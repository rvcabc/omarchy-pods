// The opcode 0x0E audio-source frame: who holds the pods, with the MAC byte-reversed on the wire.

#include "audiosource.hpp"

#include <QtTest/QtTest>

using namespace OpenPods::AudioSource;

class TestAudioSource : public QObject
{
    Q_OBJECT

private slots:
    void mediaFrame_parsesMacAndType()
    {
        const auto info = parse(QByteArray::fromHex("040004000e07d82ef215ef0c02"));
        QVERIFY(info.has_value());
        QCOMPARE(info->deviceMac.toHex(), QByteArray("d82ef215ef0c"));
        QCOMPARE(info->type, Type::Media);
    }

    void callAndNone_parse()
    {
        QCOMPARE(parse(QByteArray::fromHex("040004000e07aabbccddeeff01"))->type, Type::Call);
        QCOMPARE(parse(QByteArray::fromHex("040004000e07aabbccddeeff00"))->type, Type::None);
        QCOMPARE(parse(QByteArray::fromHex("040004000e07aabbccddeeff09"))->type, Type::Unknown);
    }

    void shortOrForeignFrame_isRejected()
    {
        QVERIFY(!parse(QByteArray::fromHex("040004000e07aabbccddeeff")).has_value());
        QVERIFY(!parse(QByteArray::fromHex("040004000400aabbccddeeff02")).has_value());
    }

    void reversedMac_matchesTheWireOrder()
    {
        QCOMPARE(reversedMac(QStringLiteral("0C:EF:15:F2:2E:D8")).toHex(), QByteArray("d82ef215ef0c"));
        const auto info = parse(QByteArray::fromHex("040004000e07d82ef215ef0c02"));
        QVERIFY(info.has_value());
        QCOMPARE(info->deviceMac, reversedMac(QStringLiteral("0C:EF:15:F2:2E:D8")));
    }

    void typeNameAndMacTail()
    {
        QCOMPARE(typeName(Type::Media), QStringLiteral("media"));
        QCOMPARE(typeName(Type::Unknown), QStringLiteral("unknown"));
        QCOMPARE(macTail(QByteArray::fromHex("d82ef215ef0c")), QStringLiteral("ef:0c"));
        QCOMPARE(macTail(QByteArray()), QStringLiteral("??:??"));
    }
};

QTEST_GUILESS_MAIN(TestAudioSource)
#include "tst_audiosource.moc"
