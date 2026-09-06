// Pure-helper tests for OpenPods::parseMetadata, the opcode 0x1D
// string-frame parser extracted from main.cpp so it runs without the daemon.

#include "metadata.hpp"

#include <QtTest/QtTest>

namespace
{
QByteArray frameWithStrings(const QList<QByteArray> &strings)
{
    QByteArray frame = AirPodsPackets::Parse::METADATA;
    frame.append(QByteArray(OpenPods::metadataPrefixBytes, '\0'));
    for (const QByteArray &string : strings) {
        frame.append(string);
        frame.append('\0');
    }
    return frame;
}

// Every serial here is invented; the real capture replaces this frame once scrubbed.
QByteArray syntheticFrame()
{
    QByteArray frame = frameWithStrings({
        "AirPods Pro", "A2698", "Apple Inc.", "H1ABCDEFGHIJ", "6E188", "6E188", "1.0.0",
        "com.apple.accessory.updater.app.71", "H1LEFT000001", "H1RIGHT00001", "6E188",
    });
    frame.append(QByteArray::fromHex("0102"));
    return frame;
}

// Three strings is all main.cpp reads today, and older firmware may send no more than that.
QByteArray threeStringFrame()
{
    return frameWithStrings({"AirPods Pro", "A2698", "Apple Inc."});
}
}

class TestMetadata : public QObject
{
    Q_OBJECT

private slots:
    void syntheticFrame_parsesEveryField()
    {
        const OpenPods::MetadataParse out = OpenPods::parseMetadata(syntheticFrame());
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.value.name, QStringLiteral("AirPods Pro"));
        QCOMPARE(out.value.modelNumber, QStringLiteral("A2698"));
        QCOMPARE(out.value.manufacturer, QStringLiteral("Apple Inc."));
        QCOMPARE(out.value.serialNumber, QStringLiteral("H1ABCDEFGHIJ"));
        QCOMPARE(out.value.firmwareVersion, QStringLiteral("6E188"));
        QCOMPARE(out.value.firmwareVersion2, QStringLiteral("6E188"));
        QCOMPARE(out.value.hardwareRevision, QStringLiteral("1.0.0"));
        QCOMPARE(out.value.updaterIdentifier, QStringLiteral("com.apple.accessory.updater.app.71"));
        QCOMPARE(out.value.leftSerial, QStringLiteral("H1LEFT000001"));
        QCOMPARE(out.value.rightSerial, QStringLiteral("H1RIGHT00001"));
        QCOMPARE(out.value.numericVersion, QStringLiteral("6E188"));
    }

    void threeStringFrame_parsesWithEmptyLaterFields()
    {
        const OpenPods::MetadataParse out = OpenPods::parseMetadata(threeStringFrame());
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.value.name, QStringLiteral("AirPods Pro"));
        QCOMPARE(out.value.modelNumber, QStringLiteral("A2698"));
        QCOMPARE(out.value.manufacturer, QStringLiteral("Apple Inc."));
        QVERIFY(out.value.serialNumber.isEmpty());
        QVERIFY(out.value.firmwareVersion.isEmpty());
        QVERIFY(out.value.firmwareVersion2.isEmpty());
        QVERIFY(out.value.hardwareRevision.isEmpty());
        QVERIFY(out.value.updaterIdentifier.isEmpty());
        QVERIFY(out.value.leftSerial.isEmpty());
        QVERIFY(out.value.rightSerial.isEmpty());
        QVERIFY(out.value.numericVersion.isEmpty());
    }

    void longUtf8Name_parsesLaterFields()
    {
        // 32 CJK characters pass renameAirPods's 32-QChar cap yet arrive as 96 UTF-8 bytes.
        const QByteArray name = QString(32, QChar(0x4E2D)).toUtf8();
        QCOMPARE(name.size(), 96);
        const OpenPods::MetadataParse out =
            OpenPods::parseMetadata(frameWithStrings({name, "A2698", "Apple Inc."}));
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.value.name, QString::fromUtf8(name));
        QCOMPARE(out.value.modelNumber, QStringLiteral("A2698"));
        QCOMPARE(out.value.manufacturer, QStringLiteral("Apple Inc."));
    }

    void wrongHeader_fails()
    {
        QByteArray frame = syntheticFrame();
        frame.replace(0, AirPodsPackets::Parse::BATTERY_STATUS.size(),
                      AirPodsPackets::Parse::BATTERY_STATUS);
        const OpenPods::MetadataParse out = OpenPods::parseMetadata(frame);
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains(QStringLiteral("header")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QStringLiteral("0400040004")), qPrintable(out.error));
    }

    void tooShortFrame_failsNamingSize()
    {
        // Header plus three of the six prefix bytes: startsWith passes, the prefix does not fit.
        QByteArray frame = AirPodsPackets::Parse::METADATA;
        frame.append(QByteArray(3, '\0'));
        const OpenPods::MetadataParse out = OpenPods::parseMetadata(frame);
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains(QStringLiteral("8 bytes")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QStringLiteral("needs 11")), qPrintable(out.error));
    }

    void unterminatedField_failsNamingField()
    {
        // Cut mid-serialNumber: a partial serial must fail rather than be handed to the caller as a value.
        QByteArray frame = threeStringFrame();
        frame.append("H1ABC");
        const OpenPods::MetadataParse out = OpenPods::parseMetadata(frame);
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains(QStringLiteral("serialNumber")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QStringLiteral("5 bytes")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QStringLiteral("without NUL")), qPrintable(out.error));
    }

    void overLongField_failsNamingLimit()
    {
        // NUL-terminated but past the one-byte name length the rename packet can carry.
        const int runBytes = 300;
        const OpenPods::MetadataParse out = OpenPods::parseMetadata(
            frameWithStrings({QByteArray(runBytes, 'A'), "A2698", "Apple Inc."}));
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains(QStringLiteral("name")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QStringLiteral("300 bytes")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QString::number(OpenPods::metadataMaxFieldBytes)),
                 qPrintable(out.error));
        QVERIFY2(!out.error.contains(QStringLiteral("without NUL")), qPrintable(out.error));
    }
};

QTEST_GUILESS_MAIN(TestMetadata)
#include "tst_metadata.moc"
