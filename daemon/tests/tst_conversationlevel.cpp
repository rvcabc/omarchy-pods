// Pure-helper tests for OpenPods::Conversation: the level table for the
// opcode 0x4B stream, the frame parser and the duck/restore tracker that
// mediacontroller.cpp drives.

#include "conversationlevel.hpp"

#include <QtTest/QtTest>

using OpenPods::Conversation::Action;
using OpenPods::Conversation::LevelTracker;

class TestConversationLevel : public QObject
{
    Q_OBJECT

private slots:
    void levelTable_zeroToTen()
    {
        // The defect this replaces: 1 ducked, 8 and 9 were treated as toggles and
        // every other level restored, so 2 never ducked and 3, 4, 5, 7 restored early.
        const Action expected[] = {
            Action::Ignore,  // 0
            Action::Duck,    // 1
            Action::Duck,    // 2
            Action::Ignore,  // 3
            Action::Ignore,  // 4
            Action::Ignore,  // 5
            Action::Restore, // 6
            Action::Ignore,  // 7
            Action::Restore, // 8
            Action::Restore, // 9
            Action::Ignore,  // 10
        };
        for (quint8 level = 0; level <= 10; ++level) {
            QCOMPARE(static_cast<int>(OpenPods::Conversation::actionForLevel(level)),
                     static_cast<int>(expected[level]));
        }
    }

    void levelTable_highByteIsIgnore()
    {
        // The switch has no upper bound, so the top of the byte range must land in the default.
        QCOMPARE(static_cast<int>(OpenPods::Conversation::actionForLevel(0xFF)),
                 static_cast<int>(Action::Ignore));
    }

    void levelFromFrame_acceptsTheTenByteSample()
    {
        const QByteArray frame = QByteArray::fromHex("040004004B0002000103");
        QCOMPARE(frame.size(), OpenPods::Conversation::conversationFrameBytes);
        QCOMPARE(OpenPods::Conversation::levelFromFrame(frame).value_or(0xFF), 3);
    }

    void levelFromFrame_rejectsNineBytes()
    {
        const QByteArray frame = QByteArray::fromHex("040004004B00020001");
        QVERIFY(!OpenPods::Conversation::levelFromFrame(frame).has_value());
    }

    void levelFromFrame_rejectsElevenBytes()
    {
        const QByteArray frame = QByteArray::fromHex("040004004B000200010300");
        QVERIFY(!OpenPods::Conversation::levelFromFrame(frame).has_value());
    }

    void levelFromFrame_rejectsWrongOpcode()
    {
        // Same length, opcode 0x09 in byte 4 instead of 0x4B.
        const QByteArray frame = QByteArray::fromHex("04000400090002000103");
        QVERIFY(!OpenPods::Conversation::levelFromFrame(frame).has_value());
    }

    void levelFromFrame_rejectsWrongTail()
    {
        // Same length and opcode, byte 8 is 00 instead of 01, so a check that stops at the opcode would accept it.
        const QByteArray frame = QByteArray::fromHex("040004004B0002000003");
        QVERIFY(!OpenPods::Conversation::levelFromFrame(frame).has_value());
    }

    void duckThenRestore()
    {
        LevelTracker tracker;
        QVERIFY(!tracker.ducked());

        const LevelTracker::Decision duck = tracker.onLevel(1);
        QCOMPARE(static_cast<int>(duck.action), static_cast<int>(Action::Duck));
        QVERIFY(duck.armTimeout);
        QVERIFY(!duck.cancelTimeout);
        QCOMPARE(duck.logLine, QStringLiteral("CA level 1: duck"));
        QVERIFY(tracker.ducked());

        const LevelTracker::Decision restore = tracker.onLevel(6);
        QCOMPARE(static_cast<int>(restore.action), static_cast<int>(Action::Restore));
        QVERIFY(!restore.armTimeout);
        QVERIFY(restore.cancelTimeout);
        QCOMPARE(restore.logLine, QStringLiteral("CA level 6: restore"));
        QVERIFY(!tracker.ducked());
    }

    void duckIntermediateDuck_doesNotDoubleDuck()
    {
        LevelTracker tracker;
        QCOMPARE(static_cast<int>(tracker.onLevel(1).action), static_cast<int>(Action::Duck));

        const LevelTracker::Decision intermediate = tracker.onLevel(3);
        QCOMPARE(static_cast<int>(intermediate.action), static_cast<int>(Action::Ignore));
        QVERIFY(intermediate.armTimeout);
        QVERIFY(!intermediate.cancelTimeout);
        QCOMPARE(intermediate.logLine, QStringLiteral("CA level 3: ignore, still ducked"));

        // The saved volume must survive: a second Duck is Ignore for the volume but re-arms the timeout.
        const LevelTracker::Decision again = tracker.onLevel(2);
        QCOMPARE(static_cast<int>(again.action), static_cast<int>(Action::Ignore));
        QVERIFY(again.armTimeout);
        QVERIFY(!again.cancelTimeout);
        QCOMPARE(again.logLine, QStringLiteral("CA level 2: ignore, already ducked"));
        QVERIFY(tracker.ducked());
    }

    void timeoutAfterDuck_restoresAndListsTheLevels()
    {
        LevelTracker tracker;
        tracker.onLevel(1);
        tracker.onLevel(3);
        tracker.onLevel(3);

        const LevelTracker::Decision timeout = tracker.onTimeout();
        QCOMPARE(static_cast<int>(timeout.action), static_cast<int>(Action::Restore));
        QVERIFY(!timeout.armTimeout);
        QVERIFY(!timeout.cancelTimeout);
        QCOMPARE(timeout.logLine, QStringLiteral("CA restore by timeout after levels 1,3,3"));
        QVERIFY(!tracker.ducked());

        // The list belongs to that duck; the next one starts over.
        tracker.onLevel(2);
        QCOMPARE(tracker.onTimeout().logLine,
                 QStringLiteral("CA restore by timeout after levels 2"));
    }

    void longDuckedRun_boundsTheTimeoutLogLine()
    {
        LevelTracker tracker;
        tracker.onLevel(1);
        const int framesNotLogged = 5;
        for (int frame = 1; frame < OpenPods::Conversation::maxLevelsLoggedPerDuck + framesNotLogged; ++frame) {
            QVERIFY(tracker.onLevel(3).armTimeout);
        }

        QStringList loggedLevels{QStringLiteral("1")};
        for (int frame = 1; frame < OpenPods::Conversation::maxLevelsLoggedPerDuck; ++frame) {
            loggedLevels.append(QStringLiteral("3"));
        }
        const LevelTracker::Decision timeout = tracker.onTimeout();
        QCOMPARE(static_cast<int>(timeout.action), static_cast<int>(Action::Restore));
        QCOMPARE(timeout.logLine,
                 QStringLiteral("CA restore by timeout after levels %1 and %2 more")
                     .arg(loggedLevels.join(QLatin1Char(',')))
                     .arg(framesNotLogged));
        QVERIFY(!tracker.ducked());

        // The overflow count belongs to that duck; the next one starts over.
        tracker.onLevel(2);
        QCOMPARE(tracker.onTimeout().logLine,
                 QStringLiteral("CA restore by timeout after levels 2"));
    }

    void timeoutWhileIdle_isIgnore()
    {
        LevelTracker tracker;
        const LevelTracker::Decision timeout = tracker.onTimeout();
        QCOMPARE(static_cast<int>(timeout.action), static_cast<int>(Action::Ignore));
        QVERIFY(!timeout.armTimeout);
        QVERIFY(!timeout.cancelTimeout);
        QCOMPARE(timeout.logLine, QStringLiteral("CA restore timeout fired while not ducked"));
        QVERIFY(!tracker.ducked());
    }

    void restoreCancelsTheTimeout()
    {
        LevelTracker tracker;
        tracker.onLevel(1);
        QVERIFY(tracker.onLevel(9).cancelTimeout);

        // A timer that fired anyway after the cancel must not restore a second time.
        QCOMPARE(static_cast<int>(tracker.onTimeout().action), static_cast<int>(Action::Ignore));
    }

    void restoreWhileIdle_isIgnore()
    {
        LevelTracker tracker;
        const LevelTracker::Decision restore = tracker.onLevel(8);
        QCOMPARE(static_cast<int>(restore.action), static_cast<int>(Action::Ignore));
        QVERIFY(!restore.armTimeout);
        QVERIFY(!restore.cancelTimeout);
        QCOMPARE(restore.logLine, QStringLiteral("CA level 8: ignore"));
        QVERIFY(!tracker.ducked());
    }

    void intermediateWhileIdle_armsNothing()
    {
        LevelTracker tracker;
        const LevelTracker::Decision ramp = tracker.onLevel(4);
        QCOMPARE(static_cast<int>(ramp.action), static_cast<int>(Action::Ignore));
        QVERIFY(!ramp.armTimeout);
        QVERIFY(!ramp.cancelTimeout);
        QCOMPARE(ramp.logLine, QStringLiteral("CA level 4: ignore"));
    }
};

QTEST_GUILESS_MAIN(TestConversationLevel)
#include "tst_conversationlevel.moc"
