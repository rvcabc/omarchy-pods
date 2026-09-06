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

    void settingVerbs_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("choice");
        QTest::addColumn<int>("number");

        QTest::newRow("allowoff:on") << QStringLiteral("allowoff:on") << QStringLiteral("allowoff") << QStringLiteral("on") << 0;
        QTest::newRow("holdmodes:7") << QStringLiteral("holdmodes:7") << QStringLiteral("holdmodes") << QString() << 7;
        QTest::newRow("holdmodes:15") << QStringLiteral("holdmodes:15") << QStringLiteral("holdmodes") << QString() << 15;
        // The choice itself carries a colon, so the parser must split only on the family's own separator.
        QTest::newRow("hold:left:noise")
            << QStringLiteral("hold:left:noise") << QStringLiteral("hold") << QStringLiteral("left:noise") << 0;
        QTest::newRow("hold:right:noise")
            << QStringLiteral("hold:right:noise") << QStringLiteral("hold") << QStringLiteral("right:noise") << 1;
        // The mic numbers are the wire bytes: this id is zero based, not the on/off convention.
        QTest::newRow("mic:auto") << QStringLiteral("mic:auto") << QStringLiteral("mic") << QStringLiteral("auto") << 0;
        QTest::newRow("mic:right") << QStringLiteral("mic:right") << QStringLiteral("mic") << QStringLiteral("right") << 1;
        QTest::newRow("mic:left") << QStringLiteral("mic:left") << QStringLiteral("mic") << QStringLiteral("left") << 2;
        QTest::newRow("eardetect:off")
            << QStringLiteral("eardetect:off") << QStringLiteral("eardetect") << QStringLiteral("off") << 0;
        QTest::newRow("swipe:on") << QStringLiteral("swipe:on") << QStringLiteral("swipe") << QStringLiteral("on") << 0;
        QTest::newRow("swipespeed:longest")
            << QStringLiteral("swipespeed:longest") << QStringLiteral("swipespeed") << QStringLiteral("longest") << 2;
        QTest::newRow("pvol:on") << QStringLiteral("pvol:on") << QStringLiteral("pvol") << QStringLiteral("on") << 0;
        QTest::newRow("tone:40") << QStringLiteral("tone:40") << QStringLiteral("tone") << QString() << 40;
        QTest::newRow("pressspeed:slower")
            << QStringLiteral("pressspeed:slower") << QStringLiteral("pressspeed") << QStringLiteral("slower") << 1;
        QTest::newRow("holdduration:shortest")
            << QStringLiteral("holdduration:shortest") << QStringLiteral("holdduration") << QStringLiteral("shortest") << 2;
        QTest::newRow("casetone:off") << QStringLiteral("casetone:off") << QStringLiteral("casetone") << QStringLiteral("off") << 0;
        QTest::newRow("sleep:on") << QStringLiteral("sleep:on") << QStringLiteral("sleep") << QStringLiteral("on") << 0;
        QTest::newRow("autoconnect:on")
            << QStringLiteral("autoconnect:on") << QStringLiteral("autoconnect") << QStringLiteral("on") << 0;
        QTest::newRow("allowautoconnect:off")
            << QStringLiteral("allowautoconnect:off") << QStringLiteral("allowautoconnect") << QStringLiteral("off") << 0;
    }

    void settingVerbs()
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

    void settingVerbs_badInput_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<QByteArray>("reply");

        QTest::newRow("holdmodes:0") << QStringLiteral("holdmodes:0") << QByteArrayLiteral("error: holdmodes needs a value 1..15\n");
        QTest::newRow("holdmodes:16") << QStringLiteral("holdmodes:16") << QByteArrayLiteral("error: holdmodes needs a value 1..15\n");
        QTest::newRow("tone:101") << QStringLiteral("tone:101") << QByteArrayLiteral("error: tone needs a value 0..100\n");
        // Siri is read back from the pods but never written, so the row does not list it.
        QTest::newRow("hold:left:siri")
            << QStringLiteral("hold:left:siri") << QByteArrayLiteral("error: hold needs one of left:noise|right:noise\n");
        QTest::newRow("mic:both") << QStringLiteral("mic:both") << QByteArrayLiteral("error: mic needs one of auto|right|left\n");
        QTest::newRow("swipespeed:fast")
            << QStringLiteral("swipespeed:fast") << QByteArrayLiteral("error: swipespeed needs one of default|longer|longest\n");
        QTest::newRow("allowautoconnect:maybe")
            << QStringLiteral("allowautoconnect:maybe") << QByteArrayLiteral("error: allowautoconnect needs on or off\n");
        QTest::newRow("eq:") << QStringLiteral("eq:") << QByteArrayLiteral("error: eq needs 1..32 bytes of text\n");
    }

    void settingVerbs_badInput()
    {
        QFETCH(QString, message);
        QFETCH(QByteArray, reply);

        const Parsed parsed = parseVerb(message);
        QVERIFY(!parsed.ok);
        QCOMPARE(parsed.reply, reply);
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

    // The rename row is Kind::Text, so the byte count, not the character count, decides.
    void renameRow_countsUtf8Bytes()
    {
        const Parsed named = parseVerb(QStringLiteral("rename:Bryce's Pods"));
        QVERIFY2(named.ok, named.reply.constData());
        QCOMPARE(named.verb, QStringLiteral("rename"));
        QCOMPARE(named.text, QStringLiteral("Bryce's Pods"));
        QVERIFY(named.choice.isEmpty());

        const Parsed empty = parseVerb(QStringLiteral("rename:"));
        QVERIFY(!empty.ok);
        QCOMPARE(empty.reply, QByteArrayLiteral("error: rename needs 1..32 bytes of text\n"));

        // Seventeen two-byte characters is 34 bytes, so the byte limit trips where a character count would not.
        const int twoByteCharacters = 17;
        const QChar eAcute(0xE9);
        const Parsed wide = parseVerb(QStringLiteral("rename:") + QString(twoByteCharacters, eAcute));
        QVERIFY(!wide.ok);
        QCOMPARE(wide.reply, QByteArrayLiteral("error: rename needs 1..32 bytes of text\n"));

        // Sixteen of them is exactly 32 bytes and passes.
        const Parsed widest = parseVerb(QStringLiteral("rename:") + QString(twoByteCharacters - 1, eAcute));
        QVERIFY2(widest.ok, widest.reply.constData());
        QCOMPARE(widest.text.toUtf8().size(), 32);
    }

    // The eq row only passes the text through; PodSettings::parseEq gives a bad band its own reason.
    void eqRow_passesTextThrough()
    {
        const Parsed eq = parseVerb(QStringLiteral("eq:on:50:50:50"));
        QVERIFY2(eq.ok, eq.reply.constData());
        QCOMPARE(eq.verb, QStringLiteral("eq"));
        QCOMPARE(eq.text, QStringLiteral("on:50:50:50"));
        QVERIFY(eq.choice.isEmpty());
        QCOMPARE(eq.number, 0);
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
            QStringLiteral("  reopen                  Reopen the daemon window (a headless daemon refuses)"),
            QStringLiteral("  status                  Print one-line JSON status snapshot to stdout"),
            QStringLiteral("  forget                  Run `bluetoothctl remove` on the connected device"),
            QStringLiteral("  disconnect              bluetoothctl disconnect on the paired AirPods"),
            QStringLiteral("  connect                 bluetoothctl connect on the paired AirPods"),
            QStringLiteral("  noise:MODE              Set noise control: off, anc, transparency, adaptive, or cycle (Off->ANC->Trans->Adaptive)"),
            QStringLiteral("  ear:MODE                Ear-detection auto-pause: off, one (pause when either pod is removed, default), both (only when both are)"),
            QStringLiteral("  ca:on|off               Conversation Awareness (Pro2 only)"),
            QStringLiteral("  onebud:on|off           One-Bud ANC (Pro2+: keep ANC active with only one pod in)"),
            QStringLiteral("  adaptive:N              Set Adaptive Noise level 0-100 (Pro2/Pro3, only while noise_mode=Adaptive)"),
            QStringLiteral("  allowoff:on|off         Allow Off in the noise control cycle"),
            QStringLiteral("  holdmodes:N             Modes the stem hold cycles, as a bitmask: 1 Off, 2 ANC, 4 Transparency, 8 Adaptive (at least two)"),
            QStringLiteral("  hold:MODE               Stem hold per bud: left:noise or right:noise (the other bud keeps its setting)"),
            QStringLiteral("  mic:MODE                Microphone: auto, right or left"),
            QStringLiteral("  eardetect:on|off        Automatic ear detection on the buds"),
            QStringLiteral("  swipe:on|off            Volume swipe on the stem"),
            QStringLiteral("  swipespeed:MODE         Volume swipe length: default, longer or longest"),
            QStringLiteral("  pvol:on|off             Personalized Volume"),
            QStringLiteral("  tone:N                  Tone volume 0-100"),
            QStringLiteral("  pressspeed:MODE         Press speed: default, slower or slowest"),
            QStringLiteral("  holdduration:MODE       Press and hold duration: default, shorter or shortest"),
            QStringLiteral("  casetone:on|off         Charging case sounds"),
            QStringLiteral("  sleep:on|off            Sleep detection: pause audio when you fall asleep"),
            QStringLiteral("  autoconnect:on|off      Connect to this computer automatically"),
            QStringLiteral("  allowautoconnect:on|off Allow automatic connection"),
            QStringLiteral("  rename:TEXT             Rename the AirPods (1 to 32 UTF-8 bytes)"),
            QStringLiteral("  notify:MODE             Desktop toasts: on|off for all of them, connected:on|off for the battery banner on connect"),
            QStringLiteral("  follow:on|off           Make the AirPods the default output when they connect"),
            QStringLiteral("  handoff:MODE            Handoff: connectonplay:on|off pulls the pods off another device when playback starts here"),
            QStringLiteral("  hearingaid:on|off       Hearing Aid (needs an enrolled audiogram and the Apple DeviceID)"),
            QStringLiteral("  hearingassist:on|off    Hearing Assistance (needs the Apple DeviceID)"),
            QStringLiteral("  lsr:on|off              Loud Sound Reduction over ATT (needs the Apple DeviceID)"),
            QStringLiteral("  eq:TEXT                 Custom EQ: on|off:low:mid:high, each 0-100"),
        };
        const QStringList lines = usageLines();
        QCOMPARE(lines.size(), verbTable().size());
        QCOMPARE(lines, expected);
    }
};

QTEST_GUILESS_MAIN(TestVerbTable)
#include "tst_verbtable.moc"
