// Tests for OpenPods::ControlCommandState, the recorder main.cpp feeds every socket read so a feature can prove its echo.

#include "controlcommandstate.hpp"

#include <QtTest/QtTest>

namespace
{
// 04 00 04 00 09 00 34 01 00 00 00: allow-off enabled, the daemon's own 11-byte shape.
const QByteArray allowOffEnabled = QByteArray::fromHex("0400040009003401000000");

// 04 00 04 00 09 00 0d 02 00 00 00: listening mode noise cancellation, an id below 0x34 for the ordering case.
const QByteArray listeningModeNoiseCancellation = QByteArray::fromHex("0400040009000d02000000");
}

class TestControlCommandState : public QObject
{
    Q_OBJECT

private slots:
    void elevenByteFrame_recordsIdAndPayload()
    {
        OpenPods::ControlCommandState state;
        const auto recorded = state.record(allowOffEnabled);
        QVERIFY(recorded.accepted);
        QVERIFY(!recorded.warning.has_value());
        QCOMPARE(state.idsSeen(), (QList<quint8>{0x34}));
        QCOMPARE(state.payload(0x34).value_or(QByteArray()), QByteArray::fromHex("01000000"));
    }

    void differentHeader_rejected()
    {
        OpenPods::ControlCommandState state;
        // Opcode 0x04 where 0x09 belongs, so this is not a control command at all.
        const auto recorded = state.record(QByteArray::fromHex("0400040004003401000000"));
        QVERIFY(!recorded.accepted);
        QVERIFY(!recorded.warning.has_value());
        QVERIFY(state.idsSeen().isEmpty());
        QVERIFY(!state.payload(0x34).has_value());
    }

    void sevenByteFrame_rejected()
    {
        OpenPods::ControlCommandState state;
        const QByteArray truncated = allowOffEnabled.left(7);
        QCOMPARE(truncated.size(), 7);
        const auto recorded = state.record(truncated);
        QVERIFY(!recorded.accepted);
        QVERIFY(!recorded.warning.has_value());
        QVERIFY(state.idsSeen().isEmpty());
        QVERIFY(!state.payload(0x34).has_value());
    }

    // Eight bytes is the floor parseActive accepts, so this pins the boundary the 7-byte case only approaches.
    void eightByteFrame_recordedWithWarning()
    {
        OpenPods::ControlCommandState state;
        const QByteArray frame = QByteArray::fromHex("0400040009003401");
        QCOMPARE(frame.size(), 8);
        const auto recorded = state.record(frame);
        QVERIFY(recorded.accepted);
        QCOMPARE(recorded.warning.value_or(QString()),
                 QStringLiteral("Control command 0x34 arrived with 8 bytes, expected 11"));
        QCOMPARE(state.idsSeen(), (QList<quint8>{0x34}));
        QCOMPARE(state.payload(0x34).value_or(QByteArray()), QByteArray::fromHex("01"));
    }

    void thirteenByteFrame_recordedWithWarning()
    {
        OpenPods::ControlCommandState state;
        const QByteArray frame = QByteArray::fromHex("04000400090024010000000000");
        QCOMPARE(frame.size(), 13);
        const auto recorded = state.record(frame);
        QVERIFY(recorded.accepted);
        QCOMPARE(recorded.warning.value_or(QString()),
                 QStringLiteral("Control command 0x24 arrived with 13 bytes, expected 11"));
        QCOMPARE(state.idsSeen(), (QList<quint8>{0x24}));
        QCOMPARE(state.payload(0x24).value_or(QByteArray()), QByteArray::fromHex("010000000000"));
    }

    // Six ids in scrambled order: two keys could come out ascending from an unordered container by chance.
    void idsSeen_sortsAscending()
    {
        OpenPods::ControlCommandState state;
        for (const quint8 id : {0x34, 0x06, 0x2E, 0x0D, 0x28, 0x1B}) {
            QVERIFY(state.record(ControlCommand::createCommand(id, 0x01)).accepted);
        }
        QCOMPARE(state.idsSeen(), (QList<quint8>{0x06, 0x0D, 0x1B, 0x28, 0x2E, 0x34}));
    }

    void newerPayload_overwritesOlder()
    {
        OpenPods::ControlCommandState state;
        QVERIFY(state.record(allowOffEnabled).accepted);
        QVERIFY(state.record(QByteArray::fromHex("0400040009003402000000")).accepted);
        QCOMPARE(state.idsSeen(), (QList<quint8>{0x34}));
        QCOMPARE(state.payload(0x34).value_or(QByteArray()), QByteArray::fromHex("02000000"));
    }

    void clear_emptiesEverything()
    {
        OpenPods::ControlCommandState state;
        QVERIFY(state.record(allowOffEnabled).accepted);
        QVERIFY(state.record(listeningModeNoiseCancellation).accepted);
        state.clear();
        QVERIFY(state.idsSeen().isEmpty());
        QVERIFY(!state.payload(0x34).has_value());
        QVERIFY(!state.payload(0x0D).has_value());
    }
};

QTEST_GUILESS_MAIN(TestControlCommandState)
#include "tst_controlcommandstate.moc"
