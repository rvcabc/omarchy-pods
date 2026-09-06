// Every verb main.cpp dispatches today must round-trip through the table, and every failure must name its reason.

#include "verbtable.hpp"

#include <QtTest/QtTest>
#include <algorithm>

using OpenPods::Ipc::Kind;
using OpenPods::Ipc::matchVerb;
using OpenPods::Ipc::Parsed;
using OpenPods::Ipc::parseVerb;
using OpenPods::Ipc::usageLines;
using OpenPods::Ipc::VerbSpec;
using OpenPods::Ipc::verbTable;

class TestVerbTable : public QObject
{
    Q_OBJECT

private slots:
    void existingVerbs_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("choice");
        QTest::addColumn<int>("number");

        QTest::newRow("reopen") << QStringLiteral("reopen") << QStringLiteral("reopen") << QString() << 0;
        QTest::newRow("status") << QStringLiteral("status") << QStringLiteral("status") << QString() << 0;
        QTest::newRow("forget") << QStringLiteral("forget") << QStringLiteral("forget") << QString() << 0;
        QTest::newRow("disconnect") << QStringLiteral("disconnect") << QStringLiteral("disconnect") << QString() << 0;
        QTest::newRow("connect") << QStringLiteral("connect") << QStringLiteral("connect") << QString() << 0;
        // The noise numbers are the NoiseControlMode values main.cpp hands setNoiseControlModeInt; cycle is the one past Adaptive.
        QTest::newRow("noise:off") << QStringLiteral("noise:off") << QStringLiteral("noise") << QStringLiteral("off") << 0;
        QTest::newRow("noise:anc") << QStringLiteral("noise:anc") << QStringLiteral("noise") << QStringLiteral("anc") << 1;
        QTest::newRow("noise:transparency")
            << QStringLiteral("noise:transparency") << QStringLiteral("noise") << QStringLiteral("transparency") << 2;
        QTest::newRow("noise:adaptive")
            << QStringLiteral("noise:adaptive") << QStringLiteral("noise") << QStringLiteral("adaptive") << 3;
        QTest::newRow("noise:cycle") << QStringLiteral("noise:cycle") << QStringLiteral("noise") << QStringLiteral("cycle") << 4;
        QTest::newRow("ear:off") << QStringLiteral("ear:off") << QStringLiteral("ear") << QStringLiteral("off") << 0;
        QTest::newRow("ear:one") << QStringLiteral("ear:one") << QStringLiteral("ear") << QStringLiteral("one") << 1;
        QTest::newRow("ear:both") << QStringLiteral("ear:both") << QStringLiteral("ear") << QStringLiteral("both") << 2;
        QTest::newRow("ca:on") << QStringLiteral("ca:on") << QStringLiteral("ca") << QStringLiteral("on") << 0;
        QTest::newRow("ca:off") << QStringLiteral("ca:off") << QStringLiteral("ca") << QStringLiteral("off") << 0;
        QTest::newRow("onebud:on") << QStringLiteral("onebud:on") << QStringLiteral("onebud") << QStringLiteral("on") << 0;
        QTest::newRow("onebud:off") << QStringLiteral("onebud:off") << QStringLiteral("onebud") << QStringLiteral("off") << 0;
    }

    void existingVerbs()
    {
        QFETCH(QString, message);
        QFETCH(QString, family);
        QFETCH(QString, choice);
        QFETCH(int, number);

        const Parsed parsed = parseVerb(message);
        QVERIFY2(parsed.ok, parsed.reply.constData());
        QCOMPARE(parsed.verb, family);
        QCOMPARE(parsed.choice, choice);
        QCOMPARE(parsed.number, number);
        QVERIFY(parsed.text.isEmpty());
        QVERIFY(parsed.reply.isEmpty());
    }

    void adaptiveFifty_parsesNumber()
    {
        const Parsed parsed = parseVerb(QStringLiteral("adaptive:50"));
        QVERIFY2(parsed.ok, parsed.reply.constData());
        QCOMPARE(parsed.verb, QStringLiteral("adaptive"));
        QCOMPARE(parsed.number, 50);
        QVERIFY(parsed.choice.isEmpty());
    }

    void adaptiveOutOfRange_data()
    {
        QTest::addColumn<QString>("message");

        QTest::newRow("above max") << QStringLiteral("adaptive:101");
        QTest::newRow("empty payload") << QStringLiteral("adaptive:");
        QTest::newRow("leading space") << QStringLiteral("adaptive: 5");
    }

    void adaptiveOutOfRange()
    {
        QFETCH(QString, message);

        const Parsed parsed = parseVerb(message);
        QVERIFY(!parsed.ok);
        QCOMPARE(parsed.reply, QByteArrayLiteral("error: adaptive needs a value 0..100\n"));
    }

    void noiseLoud_listsChoices()
    {
        const Parsed parsed = parseVerb(QStringLiteral("noise:loud"));
        QVERIFY(!parsed.ok);
        QCOMPARE(parsed.reply,
                 QByteArrayLiteral("error: noise needs one of off|anc|transparency|adaptive|cycle\n"));
    }

    void caMaybe_needsOnOrOff()
    {
        const Parsed parsed = parseVerb(QStringLiteral("ca:maybe"));
        QVERIFY(!parsed.ok);
        QCOMPARE(parsed.reply, QByteArrayLiteral("error: ca needs on or off\n"));
    }

    void unknownVerb_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<QByteArray>("reply");

        QTest::newRow("bogus") << QStringLiteral("bogus") << QByteArrayLiteral("error: unknown verb bogus\n");
        QTest::newRow("empty") << QString() << QByteArrayLiteral("error: unknown verb \n");
        // Whole-message matching: a family alone or a Bare verb with a suffix is nothing the table lists.
        QTest::newRow("family alone") << QStringLiteral("noise") << QByteArrayLiteral("error: unknown verb noise\n");
        QTest::newRow("bare with suffix")
            << QStringLiteral("status:x") << QByteArrayLiteral("error: unknown verb status:x\n");
    }

    void unknownVerb()
    {
        QFETCH(QString, message);
        QFETCH(QByteArray, reply);

        const Parsed parsed = parseVerb(message);
        QVERIFY(!parsed.ok);
        QCOMPARE(parsed.reply, reply);
    }

    // No table row is Kind::Text until rename lands, so this is the one test that builds its own row.
    void textRow_countsUtf8Bytes()
    {
        const VerbSpec rename{"rename", Kind::Text, nullptr, 1, 32, "Rename the pods"};

        const auto named = matchVerb(rename, QStringLiteral("rename:Bryce's Pods"));
        QVERIFY(named.has_value());
        QVERIFY2(named->ok, named->reply.constData());
        QCOMPARE(named->verb, QStringLiteral("rename"));
        QCOMPARE(named->text, QStringLiteral("Bryce's Pods"));

        const auto empty = matchVerb(rename, QStringLiteral("rename:"));
        QVERIFY(empty.has_value());
        QVERIFY(!empty->ok);
        QCOMPARE(empty->reply, QByteArrayLiteral("error: rename needs 1..32 bytes of text\n"));

        // Seventeen two-byte characters is 34 bytes, so the byte limit trips where a character count would not.
        const int twoByteCharacters = 17;
        const QChar eAcute(0xE9);
        const auto wide = matchVerb(rename, QStringLiteral("rename:") + QString(twoByteCharacters, eAcute));
        QVERIFY(wide.has_value());
        QVERIFY(!wide->ok);
        QCOMPARE(wide->reply, QByteArrayLiteral("error: rename needs 1..32 bytes of text\n"));
    }

    void matchVerb_skipsRowsNotAddressed()
    {
        const QList<VerbSpec> &table = verbTable();
        const auto noise = std::find_if(table.cbegin(), table.cend(), [](const VerbSpec &row) {
            return QLatin1StringView(row.verb) == QLatin1StringView("noise");
        });
        QVERIFY2(noise != table.cend(), "verbTable has no noise row");
        QVERIFY(noise->kind == Kind::Choice);
        QVERIFY(!matchVerb(*noise, QStringLiteral("status")).has_value());
        QVERIFY(!matchVerb(*noise, QStringLiteral("noise")).has_value());
        QVERIFY(matchVerb(*noise, QStringLiteral("noise:loud")).has_value());
    }

    // The whole rendering is pinned so a column or description change to any row shows up here, not in the CLI help.
    void usageLines_renderEveryRow()
    {
        const QStringList expected = {
            QStringLiteral("  reopen              Reopen the daemon window (a headless daemon refuses)"),
            QStringLiteral("  status              Print one-line JSON status snapshot to stdout"),
            QStringLiteral("  forget              Run `bluetoothctl remove` on the connected device"),
            QStringLiteral("  disconnect          bluetoothctl disconnect on the paired AirPods"),
            QStringLiteral("  connect             bluetoothctl connect on the paired AirPods"),
            QStringLiteral("  noise:MODE          Set noise control: off, anc, transparency, adaptive, or cycle (Off->ANC->Trans->Adaptive)"),
            QStringLiteral("  ear:MODE            Ear-detection auto-pause: off, one (pause when either pod is removed, default), both (only when both are)"),
            QStringLiteral("  ca:on|off           Conversation Awareness (Pro2 only)"),
            QStringLiteral("  onebud:on|off       One-Bud ANC (Pro2+: keep ANC active with only one pod in)"),
            QStringLiteral("  adaptive:N          Set Adaptive Noise level 0-100 (Pro2/Pro3, only while noise_mode=Adaptive)"),
        };
        const QStringList lines = usageLines();
        QCOMPARE(lines.size(), verbTable().size());
        QCOMPARE(lines, expected);
    }
};

QTEST_GUILESS_MAIN(TestVerbTable)
#include "tst_verbtable.moc"
