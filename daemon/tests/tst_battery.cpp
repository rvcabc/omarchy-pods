// Battery::parsePacket bounds + protocol regression.
//
// Pre-fix, parsePacket called packet[6] after only verifying the
// 6-byte BATTERY_STATUS header via startsWith. A 6-byte buffer
// (== header length, no count byte) would UB on packet[6].
// New code rejects size < 7 up front.

#include <QTest>
#include <QByteArray>
#include <QSignalSpy>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(openpods, "openpods.test", QtWarningMsg)

#include "../battery.hpp"

class TestBattery : public QObject
{
    Q_OBJECT

private slots:
    void parsePacket_rejectsEmpty()
    {
        Battery b;
        QVERIFY(!b.parsePacket(QByteArray()));
    }

    void parsePacket_rejectsShortBelowHeader()
    {
        Battery b;
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS.left(4);
        QVERIFY(!b.parsePacket(buf));
    }

    void parsePacket_rejectsHeaderOnly()
    {
        // Exactly the header (6 bytes): startsWith passes; pre-fix this
        // UB'd on packet[6].
        Battery b;
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS;
        QCOMPARE(buf.size(), 6);
        QVERIFY(!b.parsePacket(buf));
    }

    void parsePacket_rejectsBatteryCountTooHigh()
    {
        Battery b;
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS;
        buf.append(static_cast<char>(4)); // count > 3 → reject
        QVERIFY(!b.parsePacket(buf));
    }

    void parsePacket_rejectsCountSizeMismatch()
    {
        Battery b;
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS;
        buf.append(static_cast<char>(1)); // 1 battery → expect 7 + 5 = 12 bytes
        buf.append(QByteArray(3, '\0')); // only 3 more = 10 total
        QVERIFY(!b.parsePacket(buf));
    }

    void parsePacket_acceptsSingleHeadset()
    {
        Battery b;
        QSignalSpy spy(&b, &Battery::batteryStatusChanged);

        // Build a valid 1-battery packet: header + count(1) + entry(5).
        // Entry layout: type, spacer(0x01), level, status, end(0x01)
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS;
        buf.append(static_cast<char>(1));                       // count
        buf.append(static_cast<char>(Battery::Component::Headset)); // type
        buf.append(static_cast<char>(0x01));                    // spacer
        buf.append(static_cast<char>(85));                      // level
        buf.append(static_cast<char>(Battery::BatteryStatus::Discharging));
        buf.append(static_cast<char>(0x01));                    // end

        QVERIFY(b.parsePacket(buf));
        QVERIFY(spy.count() >= 1);
        QCOMPARE(b.getHeadsetLevel(), quint8(85));
        QVERIFY(b.isHeadsetAvailable());
    }

    void parsePacket_rejectsBrokenSpacer()
    {
        Battery b;
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS;
        buf.append(static_cast<char>(1));
        buf.append(static_cast<char>(Battery::Component::Left));
        buf.append(static_cast<char>(0x42)); // spacer should be 0x01
        buf.append(static_cast<char>(50));
        buf.append(static_cast<char>(Battery::BatteryStatus::Discharging));
        buf.append(static_cast<char>(0x01));
        QVERIFY(!b.parsePacket(buf));
    }

    void parseEncryptedPacket_rejectsWrongSize()
    {
        Battery b;
        // Spec: must be exactly 16 bytes.
        QVERIFY(!b.parseEncryptedPacket(QByteArray(8,  '\0'), true, false, false));
        QVERIFY(!b.parseEncryptedPacket(QByteArray(15, '\0'), true, false, false));
        QVERIFY(!b.parseEncryptedPacket(QByteArray(17, '\0'), true, false, false));
    }

    void resetClearsState()
    {
        Battery b;
        // Synthesize a packet so the indicator reports a level, then reset.
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS;
        buf.append(static_cast<char>(1));
        buf.append(static_cast<char>(Battery::Component::Headset));
        buf.append(static_cast<char>(0x01));
        buf.append(static_cast<char>(75));
        buf.append(static_cast<char>(Battery::BatteryStatus::Discharging));
        buf.append(static_cast<char>(0x01));
        QVERIFY(b.parsePacket(buf));
        QCOMPARE(b.getHeadsetLevel(), quint8(75));

        b.reset();
        QCOMPARE(b.getHeadsetLevel(), quint8(0));
        QVERIFY(!b.isHeadsetAvailable());
    }

private:
    // Sample input: a 16-byte decrypted payload, byte 1 left, byte 2 right, byte 3 case, high bit charging.
    QByteArray payload(int left, int right, int caseLevel)
    {
        QByteArray p(16, '\0');
        p[1] = static_cast<char>(left);
        p[2] = static_cast<char>(right);
        p[3] = static_cast<char>(caseLevel);
        return p;
    }

private slots:
    void encryptedCaseZero_isUnknownWhileNothingIsDockedToReadIt()
    {
        Battery b;
        QVERIFY(b.parseEncryptedPacket(payload(80, 80, 0), true, false, false));
        QVERIFY(!b.isCaseAvailable());
    }

    void encryptedCaseZero_isTrustedWhenAPodIsDocked()
    {
        Battery b;
        QVERIFY(b.parseEncryptedPacket(payload(80, 80, 0), true, true, false));
        QVERIFY(b.isCaseAvailable());
        QCOMPARE(b.getCaseLevel(), quint8(0));
    }

    void encryptedCaseLevel_survivesThePodsLeavingTheCase()
    {
        Battery b;
        QVERIFY(b.parseEncryptedPacket(payload(80, 80, 80), true, true, false));
        QCOMPARE(b.getCaseLevel(), quint8(80));

        QVERIFY(b.parseEncryptedPacket(payload(80, 80, 0), true, false, false));
        QVERIFY(b.isCaseAvailable());
        QCOMPARE(b.getCaseLevel(), quint8(80));
    }

    // Observed on an AirPods Max (USB-C, A3184): byte 1 held 100 while primaryLeft went true then false.
    void encryptedHeadset_levelSurvivesThePrimaryFlip()
    {
        Battery b;
        QVERIFY(b.parseEncryptedPacket(payload(100, 0, 0), true, false, true));
        QCOMPARE(b.getHeadsetLevel(), quint8(100));

        // Same bytes, primary flipped: the reading must not move.
        QVERIFY(b.parseEncryptedPacket(payload(100, 0, 0), false, false, true));
        QVERIFY(b.isHeadsetAvailable());
        QCOMPARE(b.getHeadsetLevel(), quint8(100));
    }

    // The unused slot reads 0, so a real 0 must stay distinguishable from unknown.
    void encryptedHeadsetZero_isARealReading()
    {
        Battery b;
        QVERIFY(b.parseEncryptedPacket(payload(0, 0, 0), true, false, true));
        QVERIFY(b.isHeadsetAvailable());
        QCOMPARE(b.getHeadsetLevel(), quint8(0));
    }

    void encryptedHeadsetUnknown_leavesTheHeadsetUnavailable()
    {
        Battery b;
        QVERIFY(b.parseEncryptedPacket(payload(0x7F, 0, 0), true, false, true));
        QVERIFY(!b.isHeadsetAvailable());
    }

    void encryptedHeadsetUnknown_leavesTheKnownLevelAlone()
    {
        Battery b;
        QVERIFY(b.parsePacket(headsetPacket(100, Battery::BatteryStatus::Discharging)));
        QCOMPARE(b.getHeadsetLevel(), quint8(100));

        // 0x7F means unknown, and slot 2's 50 is what the old first-non-unknown scan would have adopted.
        QVERIFY(b.parseEncryptedPacket(payload(0x7F, 50, 0), true, false, true));
        QCOMPARE(b.getHeadsetLevel(), quint8(100));
    }

    void encryptedHeadsetCharging_comesFromTheHighBit()
    {
        Battery b;
        QVERIFY(b.parseEncryptedPacket(payload(90, 0, 0), true, false, true));
        QCOMPARE(b.getHeadsetLevel(), quint8(90));
        QVERIFY(!b.isHeadsetCharging());

        QVERIFY(b.parseEncryptedPacket(payload(0x80 | 90, 0, 0), true, false, true));
        QCOMPARE(b.getHeadsetLevel(), quint8(90));
        QVERIFY(b.isHeadsetCharging());
    }

private:
    // The AAP battery packet for a headset, which is what establishes a real level.
    QByteArray headsetPacket(int level, Battery::BatteryStatus status)
    {
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS;
        buf.append(static_cast<char>(1));
        appendRecord(buf, Battery::Component::Headset, static_cast<quint8>(level), status);
        return buf;
    }

    void appendRecord(QByteArray &buf, Battery::Component component, quint8 level, Battery::BatteryStatus status)
    {
        buf.append(static_cast<char>(component));
        buf.append(static_cast<char>(0x01));
        buf.append(static_cast<char>(level));
        buf.append(static_cast<char>(status));
        buf.append(static_cast<char>(0x01));
    }

    // Three records in the order the pods send them: right, left, case, with fixed levels and the given statuses.
    QByteArray threeRecordFrame(Battery::BatteryStatus rightStatus, Battery::BatteryStatus leftStatus, Battery::BatteryStatus caseStatus)
    {
        const quint8 recordCount = 3;
        const quint8 podLevel = 100;
        const quint8 caseLevel = 80;
        QByteArray buf = AirPodsPackets::Parse::BATTERY_STATUS;
        buf.append(static_cast<char>(recordCount));
        appendRecord(buf, Battery::Component::Right, podLevel, rightStatus);
        appendRecord(buf, Battery::Component::Left, podLevel, leftStatus);
        appendRecord(buf, Battery::Component::Case, caseLevel, caseStatus);
        return buf;
    }

    // Sample input: 04 00 04 00 04 00 03 02 01 64 01 01 04 01 64 02 01 08 01 50 05 01, right charging, left discharging, case status 05.
    QByteArray optimizedCaseFrame()
    {
        return threeRecordFrame(Battery::BatteryStatus::Charging, Battery::BatteryStatus::Discharging, Battery::BatteryStatus::OptimizedCharging);
    }

private slots:
    // Newer firmware reports status 05 for the case once it holds at 80 percent.
    void optimizedCharging_caseIsChargingAndOptimized()
    {
        Battery b;
        const QByteArray frame = optimizedCaseFrame();
        QCOMPARE(frame.size(), 22);
        QVERIFY(b.parsePacket(frame));
        QCOMPARE(b.getCaseLevel(), quint8(80));
        QVERIFY(b.isCaseCharging());
        QVERIFY(b.isCaseOptimizedCharging());
    }

    void optimizedCharging_plainChargingPodIsNotOptimized()
    {
        Battery b;
        QVERIFY(b.parsePacket(optimizedCaseFrame()));
        QVERIFY(b.isRightPodCharging());
        QVERIFY(!b.isRightPodOptimizedCharging());
    }

    void optimizedCharging_dischargingPodIsNeither()
    {
        Battery b;
        QVERIFY(b.parsePacket(optimizedCaseFrame()));
        QVERIFY(!b.isLeftPodCharging());
        QVERIFY(!b.isLeftPodOptimizedCharging());
    }

    void optimizedCharging_plainChargingCaseIsNotOptimized()
    {
        Battery b;
        QVERIFY(b.parsePacket(threeRecordFrame(Battery::BatteryStatus::Charging, Battery::BatteryStatus::Charging, Battery::BatteryStatus::Charging)));
        QVERIFY(b.isCaseCharging());
        QVERIFY(!b.isCaseOptimizedCharging());
    }

    // Each side must read its own record, so the optimized pod is the only optimized component here.
    void optimizedCharging_leftPodReadsTheLeftRecord()
    {
        Battery b;
        QVERIFY(b.parsePacket(threeRecordFrame(Battery::BatteryStatus::Charging, Battery::BatteryStatus::OptimizedCharging, Battery::BatteryStatus::Charging)));
        QVERIFY(b.isLeftPodCharging());
        QVERIFY(b.isLeftPodOptimizedCharging());
        QVERIFY(!b.isRightPodOptimizedCharging());
        QVERIFY(!b.isCaseOptimizedCharging());
    }

    void optimizedCharging_rightPodReadsTheRightRecord()
    {
        Battery b;
        QVERIFY(b.parsePacket(threeRecordFrame(Battery::BatteryStatus::OptimizedCharging, Battery::BatteryStatus::Charging, Battery::BatteryStatus::Charging)));
        QVERIFY(b.isRightPodCharging());
        QVERIFY(b.isRightPodOptimizedCharging());
        QVERIFY(!b.isLeftPodOptimizedCharging());
        QVERIFY(!b.isCaseOptimizedCharging());
    }

    void optimizedCharging_rendersInTheStatusText()
    {
        Battery b;
        QVERIFY(b.parsePacket(optimizedCaseFrame()));
        QCOMPARE(b.getComponentStatus(Battery::Component::Case), QString("80% (Optimized charging)"));
    }

    void optimizedCharging_headsetReadsTheSameStatusByte()
    {
        Battery b;
        QVERIFY(b.parsePacket(headsetPacket(80, Battery::BatteryStatus::OptimizedCharging)));
        QVERIFY(b.isHeadsetCharging());
        QVERIFY(b.isHeadsetOptimizedCharging());
    }

    void optimizedCharging_plainChargingHeadsetIsNotOptimized()
    {
        Battery b;
        QVERIFY(b.parsePacket(headsetPacket(80, Battery::BatteryStatus::Charging)));
        QVERIFY(b.isHeadsetCharging());
        QVERIFY(!b.isHeadsetOptimizedCharging());
    }

    // 0x7F on the BLE path means unknown, so it must not demote an optimized case.
    void optimizedCharging_survivesAnUnknownEncryptedCaseReading()
    {
        Battery b;
        QVERIFY(b.parsePacket(optimizedCaseFrame()));
        QVERIFY(b.parseEncryptedPacket(payload(80, 80, 0x7F), true, false, false));
        QCOMPARE(b.getCaseLevel(), quint8(80));
        QVERIFY(b.isCaseCharging());
        QVERIFY(b.isCaseOptimizedCharging());
    }

    // The BLE charging bit cannot tell optimized from plain charging, so a set bit keeps the optimized state.
    void optimizedCharging_survivesAnEncryptedCaseReadingWithTheChargingBitSet()
    {
        Battery b;
        QVERIFY(b.parsePacket(optimizedCaseFrame()));
        QVERIFY(b.parseEncryptedPacket(payload(80, 80, 0x80 | 80), true, true, false));
        QCOMPARE(b.getCaseLevel(), quint8(80));
        QVERIFY(b.isCaseOptimizedCharging());
    }

    void optimizedCharging_survivesAnEncryptedPodReadingWithTheChargingBitSet()
    {
        Battery b;
        QVERIFY(b.parsePacket(threeRecordFrame(Battery::BatteryStatus::Charging, Battery::BatteryStatus::OptimizedCharging, Battery::BatteryStatus::Charging)));
        QVERIFY(b.parseEncryptedPacket(payload(0x80 | 100, 0x80 | 100, 0x80 | 80), true, true, false));
        QVERIFY(b.isLeftPodOptimizedCharging());
        QVERIFY(!b.isRightPodOptimizedCharging());
    }

    void optimizedCharging_survivesAnEncryptedHeadsetReadingWithTheChargingBitSet()
    {
        Battery b;
        QVERIFY(b.parsePacket(headsetPacket(80, Battery::BatteryStatus::OptimizedCharging)));
        QVERIFY(b.parseEncryptedPacket(payload(0x80 | 80, 0, 0), true, false, true));
        QVERIFY(b.isHeadsetOptimizedCharging());
    }

    // main.cpp feeds the BLE case nibble through this setter after every advertisement.
    void optimizedCharging_survivesTheBleCaseSetterWithTheChargingBitSet()
    {
        Battery b;
        QVERIFY(b.parsePacket(optimizedCaseFrame()));
        b.setCaseFromBle(80, true);
        QVERIFY(b.isCaseOptimizedCharging());
        QCOMPARE(b.getComponentStatus(Battery::Component::Case), QString("80% (Optimized charging)"));
    }

    void optimizedCharging_isNotInventedByTheBleCaseSetter()
    {
        Battery b;
        QVERIFY(b.parsePacket(threeRecordFrame(Battery::BatteryStatus::Charging, Battery::BatteryStatus::Charging, Battery::BatteryStatus::Charging)));
        b.setCaseFromBle(80, true);
        QVERIFY(b.isCaseCharging());
        QVERIFY(!b.isCaseOptimizedCharging());
    }

    // A cleared charging bit is the case coming off the charger, which ends optimized charging too.
    void optimizedCharging_endsWhenTheBleCaseSetterClearsTheChargingBit()
    {
        Battery b;
        QVERIFY(b.parsePacket(optimizedCaseFrame()));
        b.setCaseFromBle(80, false);
        QVERIFY(!b.isCaseCharging());
        QVERIFY(!b.isCaseOptimizedCharging());
    }
};

QTEST_GUILESS_MAIN(TestBattery)
#include "tst_battery.moc"
