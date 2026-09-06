#pragma once

#include "audiosource.hpp"

#include <QString>

// Apple's handoff, as far as Linux can play it: claim the pods when the user starts playing here, release them when the
// user pauses, pause here when another device takes them, and take them back when it lets go.
namespace OpenPods::Handoff
{
    enum class Origin
    {
        User,
        Daemon,
    };

    enum class Wire
    {
        None,
        Claim,
        Release,
    };

    enum class Action
    {
        Ignore,
        Interrupt,
        Resume,
    };

    // A claim the pods have not confirmed with an audio-source frame naming this box is treated as confirmed after this long.
    inline constexpr qint64 claimAckTimeoutMs = 3000;

    struct MediaDecision
    {
        Wire wire;
        QString logLine;
    };

    struct SourceDecision
    {
        Action action;
        QString logLine;
    };

    class State
    {
    public:
        // Sample: Playing from a user click -> Claim; Playing again from a browser tab appearing -> None (no edge);
        // Paused because the daemon's own ear detection paused -> None (daemon origin never writes the wire).
        MediaDecision onLocalMedia(bool playing, Origin origin)
        {
            const bool edge = playing != m_localPlaying;
            m_localPlaying = playing;
            if (!edge) {
                return {Wire::None, QStringLiteral("Media state repeated (%1), nothing to send").arg(stateName(playing))};
            }
            if (origin == Origin::Daemon) {
                return {Wire::None, QStringLiteral("Media %1 by the daemon itself, ownership unchanged").arg(stateName(playing))};
            }
            if (playing) {
                m_interrupted = false;
                return {Wire::Claim, QStringLiteral("Media playing here, claiming the pods")};
            }
            return {Wire::Release, QStringLiteral("Media paused here, releasing the pods")};
        }

        // Called only when the CLAIM packet actually went out.
        void noteClaimSent(qint64 nowMs)
        {
            ++m_claimsTotal;
            m_claimPending = true;
            m_claimSentAtMs = nowMs;
        }

        // Sample: other device MEDIA right after a NONE -> Interrupt; other device MEDIA 1 s after our own claim -> Ignore
        // (the frame belongs to the session we just took over); other device NONE while interrupted -> Resume.
        SourceDecision onAudioSource(bool otherDevice, AudioSource::Type type, qint64 nowMs)
        {
            if (!otherDevice) {
                if (type == AudioSource::Type::Media || type == AudioSource::Type::Call) {
                    m_claimPending = false;
                    m_interrupted = false;
                    m_lastOther = AudioSource::Type::None;
                    return {Action::Ignore, QStringLiteral("Audio source is this box (%1), ownership confirmed").arg(AudioSource::typeName(type))};
                }
                return {Action::Ignore, QStringLiteral("Audio source is this box (%1)").arg(AudioSource::typeName(type))};
            }

            const AudioSource::Type previous = m_lastOther;
            m_lastOther = type;
            switch (type) {
            case AudioSource::Type::Call:
                return interruptIfPlaying(QStringLiteral("a call on another device"));
            case AudioSource::Type::Media: {
                const bool claimFresh = m_claimPending && (nowMs - m_claimSentAtMs) < claimAckTimeoutMs;
                if (claimFresh) {
                    return {Action::Ignore, QStringLiteral("Other device reports media within %1 ms of our claim, treating it as the old session").arg(claimAckTimeoutMs)};
                }
                if (previous != AudioSource::Type::None) {
                    return {Action::Ignore, QStringLiteral("Other device still on %1, no new interruption").arg(AudioSource::typeName(previous))};
                }
                return interruptIfPlaying(QStringLiteral("media on another device"));
            }
            case AudioSource::Type::None:
                if (m_interrupted) {
                    return {Action::Resume, QStringLiteral("Other device released the pods, reclaiming and resuming")};
                }
                return {Action::Ignore, QStringLiteral("Other device released the pods, nothing of ours was interrupted")};
            case AudioSource::Type::Unknown:
                break;
            }
            return {Action::Ignore, QStringLiteral("Other device sent an unknown audio source type")};
        }

        // Called after the reclaim CLAIM went out and the players were told to play.
        void noteResumed() { m_interrupted = false; }

        bool localPlaying() const { return m_localPlaying; }
        bool interrupted() const { return m_interrupted; }
        int claimsTotal() const { return m_claimsTotal; }
        int interruptionsTotal() const { return m_interruptionsTotal; }
        AudioSource::Type lastOtherDeviceType() const { return m_lastOther; }

    private:
        SourceDecision interruptIfPlaying(const QString &reason)
        {
            if (!m_localPlaying) {
                return {Action::Ignore, QStringLiteral("Pods taken by %1 while nothing plays here").arg(reason)};
            }
            if (m_interrupted) {
                return {Action::Ignore, QStringLiteral("Already interrupted by %1").arg(reason)};
            }
            m_interrupted = true;
            ++m_interruptionsTotal;
            return {Action::Interrupt, QStringLiteral("Pods taken by %1, pausing playback here").arg(reason)};
        }

        static QString stateName(bool playing) { return playing ? QStringLiteral("playing") : QStringLiteral("paused"); }

        bool m_localPlaying = false;
        bool m_claimPending = false;
        qint64 m_claimSentAtMs = 0;
        bool m_interrupted = false;
        AudioSource::Type m_lastOther = AudioSource::Type::None;
        int m_claimsTotal = 0;
        int m_interruptionsTotal = 0;
    };
}
