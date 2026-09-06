// The handoff state machine: claims only on user-originated edges, interrupts only on a real takeover, resumes only what it paused.

#include "handoffstate.hpp"

#include <QtTest/QtTest>

using namespace OpenPods::Handoff;
using OpenPods::AudioSource::Type;

class TestHandoffState : public QObject
{
    Q_OBJECT

private slots:
    void userPlayThenPause_claimsThenReleases()
    {
        State state;
        QCOMPARE(state.onLocalMedia(true, Origin::User).wire, Wire::Claim);
        QCOMPARE(state.onLocalMedia(false, Origin::User).wire, Wire::Release);
    }

    // Browser tabs register and drop MPRIS names, which replays the same state; none of that is a user action.
    void repeatedState_sendsNothing()
    {
        State state;
        QCOMPARE(state.onLocalMedia(true, Origin::User).wire, Wire::Claim);
        QCOMPARE(state.onLocalMedia(true, Origin::User).wire, Wire::None);
        QCOMPARE(state.onLocalMedia(true, Origin::User).wire, Wire::None);
        QCOMPARE(state.onLocalMedia(false, Origin::User).wire, Wire::Release);
        QCOMPARE(state.onLocalMedia(false, Origin::User).wire, Wire::None);
    }

    // The daemon's own ear-detection pause must not hand the pods to the iPhone, and its resume must not claim twice.
    void daemonOrigin_neverWritesTheWire()
    {
        State state;
        QCOMPARE(state.onLocalMedia(true, Origin::User).wire, Wire::Claim);
        QCOMPARE(state.onLocalMedia(false, Origin::Daemon).wire, Wire::None);
        QCOMPARE(state.onLocalMedia(true, Origin::Daemon).wire, Wire::None);
        QVERIFY(state.localPlaying());
    }

    void claimsAreCountedOnlyWhenSent()
    {
        State state;
        state.onLocalMedia(true, Origin::User);
        QCOMPARE(state.claimsTotal(), 0);
        state.noteClaimSent(1000);
        QCOMPARE(state.claimsTotal(), 1);
    }

    void otherDeviceMediaAfterNone_interruptsOnlyWhilePlaying()
    {
        State idle;
        QCOMPARE(idle.onAudioSource(true, Type::Media, 5000).action, Action::Ignore);
        QCOMPARE(idle.interruptionsTotal(), 0);

        State playing;
        playing.onLocalMedia(true, Origin::User);
        playing.noteClaimSent(0);
        playing.onAudioSource(false, Type::Media, 500);
        QCOMPARE(playing.onAudioSource(true, Type::Media, 9000).action, Action::Interrupt);
        QCOMPARE(playing.interruptionsTotal(), 1);
        QVERIFY(playing.interrupted());
    }

    // The frame that names the other device right after our claim describes the session we just took, not a new one.
    void otherDeviceMediaWithinTheClaimWindow_isIgnored()
    {
        State state;
        state.onLocalMedia(true, Origin::User);
        state.noteClaimSent(10000);
        QCOMPARE(state.onAudioSource(true, Type::Media, 10000 + claimAckTimeoutMs - 1).action, Action::Ignore);
        QCOMPARE(state.interruptionsTotal(), 0);
        // Past the window, with no confirmation, the same frame is a takeover.
        QCOMPARE(state.onAudioSource(true, Type::None, 10000 + claimAckTimeoutMs).action, Action::Ignore);
        QCOMPARE(state.onAudioSource(true, Type::Media, 10000 + claimAckTimeoutMs + 1).action, Action::Interrupt);
    }

    void otherDeviceStillOnMedia_isNotASecondInterruption()
    {
        State state;
        state.onLocalMedia(true, Origin::User);
        QCOMPARE(state.onAudioSource(true, Type::Media, 9000).action, Action::Interrupt);
        state.onLocalMedia(false, Origin::Daemon);
        QCOMPARE(state.onAudioSource(true, Type::Media, 9500).action, Action::Ignore);
        QCOMPARE(state.interruptionsTotal(), 1);
    }

    void callAlwaysInterruptsPlayback()
    {
        State state;
        state.onLocalMedia(true, Origin::User);
        state.noteClaimSent(0);
        QCOMPARE(state.onAudioSource(true, Type::Call, 100).action, Action::Interrupt);
    }

    void releaseResumesOnlyWhatWasInterrupted()
    {
        State never;
        QCOMPARE(never.onAudioSource(true, Type::None, 100).action, Action::Ignore);

        State state;
        state.onLocalMedia(true, Origin::User);
        QCOMPARE(state.onAudioSource(true, Type::Media, 9000).action, Action::Interrupt);
        state.onLocalMedia(false, Origin::Daemon);
        QCOMPARE(state.onAudioSource(true, Type::None, 12000).action, Action::Resume);
        // Until the reclaim goes out the interruption stands, so a closed socket can try again on the next frame.
        QVERIFY(state.interrupted());
        state.noteResumed();
        QVERIFY(!state.interrupted());
        QCOMPARE(state.onAudioSource(true, Type::None, 13000).action, Action::Ignore);
    }

    // A user pausing here while the phone holds the pods must not resume when the phone stops.
    void userPauseDuringInterruption_cancelsTheResume()
    {
        State state;
        state.onLocalMedia(true, Origin::User);
        QCOMPARE(state.onAudioSource(true, Type::Media, 9000).action, Action::Interrupt);
        state.onLocalMedia(false, Origin::Daemon);
        state.onLocalMedia(true, Origin::User);
        QCOMPARE(state.onLocalMedia(false, Origin::User).wire, Wire::Release);
        QVERIFY(!state.interrupted());
        QCOMPARE(state.onAudioSource(true, Type::None, 12000).action, Action::Ignore);
    }

    void ourOwnMediaFrame_confirmsOwnership()
    {
        State state;
        state.onLocalMedia(true, Origin::User);
        state.noteClaimSent(0);
        QCOMPARE(state.onAudioSource(false, Type::Media, 100).action, Action::Ignore);
        // Confirmed, so another device's media is a takeover even inside the ack window.
        QCOMPARE(state.onAudioSource(true, Type::Media, 200).action, Action::Interrupt);
    }
};

QTEST_GUILESS_MAIN(TestHandoffState)
#include "tst_handoffstate.moc"
