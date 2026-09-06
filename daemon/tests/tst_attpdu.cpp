// The ATT request builders and reply parser behind the hearing controls on L2CAP PSM 31.

#include "attpdu.hpp"

#include <QtTest/QtTest>

using namespace OpenPods::Att;

class TestAttPdu : public QObject
{
    Q_OBJECT

private slots:
    void requests_matchThePythonTool()
    {
        QCOMPARE(readRequest(loudSoundReductionHandle).toHex(), QByteArray("0a1b00"));
        QCOMPARE(writeRequest(loudSoundReductionHandle, QByteArray(1, char(loudSoundReductionOn))).toHex(), QByteArray("121b0001"));
        QCOMPARE(notificationsOn(hearingAidHandle).toHex(), QByteArray("122b000100"));
        // A handle above 0xFF puts its high byte second.
        QCOMPARE(readRequest(0x0123).toHex(), QByteArray("0a2301"));
    }

    void readResponse_carriesTheValue()
    {
        const Reply reply = parseReply(QByteArray::fromHex("0b01"));
        QCOMPARE(reply.kind, Reply::Kind::ReadResponse);
        QCOMPARE(reply.value.toHex(), QByteArray("01"));
    }

    void writeResponse_isBare()
    {
        QCOMPARE(parseReply(QByteArray::fromHex("13")).kind, Reply::Kind::WriteResponse);
    }

    void error_namesRequestHandleAndCode()
    {
        const Reply reply = parseReply(QByteArray::fromHex("01121b0003"));
        QCOMPARE(reply.kind, Reply::Kind::Error);
        QCOMPARE(int(reply.requestOpcode), 0x12);
        QCOMPARE(int(reply.handle), 0x1b);
        QCOMPARE(int(reply.errorCode), 3);
        QCOMPARE(reply.describe(), QStringLiteral("ATT error 0x03 for request 0x12 on handle 0x1b"));
    }

    void notification_splitsHandleAndValue()
    {
        const Reply reply = parseReply(QByteArray::fromHex("1b2a00aabb"));
        QCOMPARE(reply.kind, Reply::Kind::Notification);
        QCOMPARE(int(reply.handle), 0x2a);
        QCOMPARE(reply.value.toHex(), QByteArray("aabb"));
    }

    void shortOrForeignPdu_isUnknownAndKept()
    {
        QCOMPARE(parseReply(QByteArray()).kind, Reply::Kind::Unknown);
        QCOMPARE(parseReply(QByteArray::fromHex("0112")).kind, Reply::Kind::Unknown);
        const Reply odd = parseReply(QByteArray::fromHex("7788"));
        QCOMPARE(odd.kind, Reply::Kind::Unknown);
        QCOMPARE(odd.value.toHex(), QByteArray("7788"));
    }
};

QTEST_GUILESS_MAIN(TestAttPdu)
#include "tst_attpdu.moc"
