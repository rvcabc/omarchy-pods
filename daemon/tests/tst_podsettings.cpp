// The settings table main.cpp sets, persists, publishes and re-asserts from, and the two non-control-command packets that share its verb table.

#include "podsettings.hpp"
#include "airpods_packets.h"
#include "verbtable.hpp"

#include <QtTest/QtTest>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

using OpenPods::PodSettings::decode;
using OpenPods::PodSettings::decodeSides;
using OpenPods::PodSettings::encode;
using OpenPods::PodSettings::encodeSides;
using OpenPods::PodSettings::eqJson;
using OpenPods::PodSettings::EqRequest;
using OpenPods::PodSettings::forId;
using OpenPods::PodSettings::forVerb;
using OpenPods::PodSettings::Kind;
using OpenPods::PodSettings::parseEq;
using OpenPods::PodSettings::refusal;
using OpenPods::PodSettings::Request;
using OpenPods::PodSettings::Spec;
using OpenPods::PodSettings::table;

namespace
{
// Listening mode echoes as a control command but belongs to the noise verb, so it must never resolve here.
constexpr quint8 listeningModeId = 0x0D;
constexpr int pinnedRowCount = 15;
}

class TestPodSettings : public QObject
{
    Q_OBJECT

private slots:
    // The whole table is pinned so a changed id, key, kind or range shows up here rather than on the wire.
    void table_pinsEveryRow_data()
    {
        QTest::addColumn<QString>("verb");
        QTest::addColumn<int>("id");
        QTest::addColumn<QString>("statusKey");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("choices");
        QTest::addColumn<int>("min");
        QTest::addColumn<int>("max");

        QTest::newRow("allowoff") << QStringLiteral("allowoff") << 0x34 << QStringLiteral("allow_off")
                                  << int(Kind::Bool) << QString() << 0 << 0;
        QTest::newRow("holdmodes") << QStringLiteral("holdmodes") << 0x1A << QStringLiteral("hold_cycle_modes")
                                   << int(Kind::Mask) << QString() << 1 << 15;
        QTest::newRow("hold") << QStringLiteral("hold") << 0x16 << QStringLiteral("hold_left")
                              << int(Kind::Sides) << QStringLiteral("left:noise|right:noise") << 0 << 0;
        QTest::newRow("mic") << QStringLiteral("mic") << 0x01 << QStringLiteral("mic_mode")
                             << int(Kind::Choice) << QStringLiteral("auto|right|left") << 0 << 0;
        QTest::newRow("eardetect") << QStringLiteral("eardetect") << 0x0A << QStringLiteral("ear_detection_on_bud")
                                   << int(Kind::Bool) << QString() << 0 << 0;
        QTest::newRow("swipe") << QStringLiteral("swipe") << 0x25 << QStringLiteral("volume_swipe")
                               << int(Kind::Bool) << QString() << 0 << 0;
        QTest::newRow("swipespeed") << QStringLiteral("swipespeed") << 0x23 << QStringLiteral("volume_swipe_speed")
                                    << int(Kind::Choice) << QStringLiteral("default|longer|longest") << 0 << 0;
        QTest::newRow("pvol") << QStringLiteral("pvol") << 0x26 << QStringLiteral("personalized_volume")
                              << int(Kind::Bool) << QString() << 0 << 0;
        QTest::newRow("tone") << QStringLiteral("tone") << 0x1F << QStringLiteral("tone_volume")
                              << int(Kind::Level) << QString() << 0 << 100;
        QTest::newRow("pressspeed") << QStringLiteral("pressspeed") << 0x17 << QStringLiteral("press_speed")
                                    << int(Kind::Choice) << QStringLiteral("default|slower|slowest") << 0 << 0;
        QTest::newRow("holdduration") << QStringLiteral("holdduration") << 0x18 << QStringLiteral("hold_duration")
                                      << int(Kind::Choice) << QStringLiteral("default|shorter|shortest") << 0 << 0;
        QTest::newRow("casetone") << QStringLiteral("casetone") << 0x31 << QStringLiteral("case_sounds")
                                  << int(Kind::Bool) << QString() << 0 << 0;
        QTest::newRow("sleep") << QStringLiteral("sleep") << 0x35 << QStringLiteral("sleep_detection")
                               << int(Kind::Bool) << QString() << 0 << 0;
        QTest::newRow("autoconnect") << QStringLiteral("autoconnect") << 0x20 << QStringLiteral("connect_automatically")
                                     << int(Kind::Bool) << QString() << 0 << 0;
        QTest::newRow("allowautoconnect") << QStringLiteral("allowautoconnect") << 0x36 << QStringLiteral("allow_auto_connect")
                                          << int(Kind::Bool) << QString() << 0 << 0;
    }

    void table_pinsEveryRow()
    {
        QFETCH(QString, verb);
        QFETCH(int, id);
        QFETCH(QString, statusKey);
        QFETCH(int, kind);
        QFETCH(QString, choices);
        QFETCH(int, min);
        QFETCH(int, max);

        QCOMPARE(table().size(), pinnedRowCount);
        const auto byVerb = forVerb(verb);
        QVERIFY2(byVerb.has_value(), qPrintable(verb));
        QCOMPARE(int(byVerb->id), id);
        QCOMPARE(QString::fromLatin1(byVerb->statusKey), statusKey);
        QCOMPARE(int(byVerb->kind), kind);
        QCOMPARE(byVerb->choices ? QString::fromLatin1(byVerb->choices) : QString(), choices);
        QCOMPARE(byVerb->min, min);
        QCOMPARE(byVerb->max, max);
        QVERIFY2(byVerb->description && *byVerb->description, qPrintable(verb));

        const auto byId = forId(byVerb->id);
        QVERIFY2(byId.has_value(), qPrintable(verb));
        QCOMPARE(QString::fromLatin1(byId->verb), verb);
    }

    void lookups_missEverythingElse()
    {
        QVERIFY(!forVerb(u"bogus").has_value());
        QVERIFY(!forVerb(u"noise").has_value());
        QVERIFY(!forVerb(u"").has_value());
        QVERIFY(!forId(listeningModeId).has_value());
    }

    void rows_haveUniqueVerbsIdsAndStatusKeys()
    {
        QSet<QString> verbs;
        QSet<quint8> ids;
        QSet<QString> statusKeys;
        for (const Spec &row : table()) {
            QVERIFY2(!verbs.contains(QString::fromLatin1(row.verb)), row.verb);
            QVERIFY2(!ids.contains(row.id), row.verb);
            QVERIFY2(!statusKeys.contains(QString::fromLatin1(row.statusKey)), row.verb);
            verbs.insert(QString::fromLatin1(row.verb));
            ids.insert(row.id);
            statusKeys.insert(QString::fromLatin1(row.statusKey));
        }
    }

    // The verb parser and this table are two copies of one list, so every row must have a mirror with the same kind, values and description.
    void verbTable_mirrorsEveryRow()
    {
        const QList<OpenPods::Ipc::VerbSpec> &verbs = OpenPods::Ipc::verbTable();
        for (const Spec &row : table()) {
            const auto mirror = std::find_if(verbs.cbegin(), verbs.cend(), [&row](const OpenPods::Ipc::VerbSpec &candidate) {
                return QLatin1StringView(candidate.verb) == QLatin1StringView(row.verb);
            });
            QVERIFY2(mirror != verbs.cend(), row.verb);
            QCOMPARE(QString::fromLatin1(mirror->description), QString::fromLatin1(row.description));
            switch (row.kind) {
            case Kind::Bool:
                QVERIFY2(mirror->kind == OpenPods::Ipc::Kind::Bool, row.verb);
                QVERIFY2(mirror->choices == nullptr, row.verb);
                break;
            case Kind::Choice:
            case Kind::Sides:
                QVERIFY2(mirror->kind == OpenPods::Ipc::Kind::Choice, row.verb);
                QCOMPARE(QString::fromLatin1(mirror->choices), QString::fromLatin1(row.choices));
                break;
            case Kind::Level:
            case Kind::Mask:
                QVERIFY2(mirror->kind == OpenPods::Ipc::Kind::Int, row.verb);
                QCOMPARE(mirror->min, row.min);
                QCOMPARE(mirror->max, row.max);
                break;
            }
        }
    }

    void renameRow_capMatchesThePacket()
    {
        QCOMPARE(OpenPods::Ipc::renameMaxBytes, AirPodsPackets::Rename::renameMaxBytes);
    }

    void encode_producesTheDocumentedBytes_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<QByteArray>("expected");

        QTest::newRow("allowoff:on") << QStringLiteral("allowoff:on") << QByteArray::fromHex("01000000");
        QTest::newRow("allowoff:off") << QStringLiteral("allowoff:off") << QByteArray::fromHex("02000000");
        QTest::newRow("mic:auto") << QStringLiteral("mic:auto") << QByteArray::fromHex("00000000");
        QTest::newRow("mic:right") << QStringLiteral("mic:right") << QByteArray::fromHex("01000000");
        QTest::newRow("mic:left") << QStringLiteral("mic:left") << QByteArray::fromHex("02000000");
        QTest::newRow("swipespeed:longest") << QStringLiteral("swipespeed:longest") << QByteArray::fromHex("02000000");
        QTest::newRow("holdduration:default") << QStringLiteral("holdduration:default") << QByteArray::fromHex("00000000");
        QTest::newRow("tone:40") << QStringLiteral("tone:40") << QByteArray::fromHex("28000000");
        QTest::newRow("tone:0") << QStringLiteral("tone:0") << QByteArray::fromHex("00000000");
        QTest::newRow("holdmodes:7") << QStringLiteral("holdmodes:7") << QByteArray::fromHex("07000000");
        QTest::newRow("holdmodes:15") << QStringLiteral("holdmodes:15") << QByteArray::fromHex("0f000000");
        // Without current bytes the generic path starts both buds at noise.
        QTest::newRow("hold:left:noise") << QStringLiteral("hold:left:noise") << QByteArray::fromHex("01010000");
    }

    // Requests come straight out of the verb parser, the only thing that builds them in the daemon.
    void encode_producesTheDocumentedBytes()
    {
        QFETCH(QString, message);
        QFETCH(QByteArray, expected);

        const OpenPods::Ipc::Parsed parsed = OpenPods::Ipc::parseVerb(message);
        QVERIFY2(parsed.ok, parsed.reply.constData());
        const auto spec = forVerb(parsed.verb);
        QVERIFY2(spec.has_value(), qPrintable(parsed.verb));
        const QByteArray data = encode(*spec, Request{parsed.choice, parsed.number});
        QCOMPARE(data.toHex(), expected.toHex());
    }

    void encode_feedsCreateCommandAnElevenByteFrame()
    {
        const auto spec = forVerb(u"allowoff");
        QVERIFY(spec.has_value());
        const QByteArray data = encode(*spec, Request{QStringLiteral("on"), 0});
        QCOMPARE(data.size(), OpenPods::PodSettings::wireDataBytes);
        const QByteArray frame = ControlCommand::createCommand(spec->id, static_cast<quint8>(data.at(0)), static_cast<quint8>(data.at(1)),
                                                               static_cast<quint8>(data.at(2)), static_cast<quint8>(data.at(3)));
        QCOMPARE(frame.toHex(), QByteArray("0400040009003401000000"));
    }

    void refusal_refusesBadMasks_data()
    {
        QTest::addColumn<int>("mask");
        QTest::addColumn<bool>("hasOff");
        QTest::addColumn<bool>("hasAdaptive");
        QTest::addColumn<QString>("expected");

        QTest::newRow("only off") << 1 << true << true << QStringLiteral("hold cycle needs at least two modes");
        QTest::newRow("only adaptive") << 8 << true << true << QStringLiteral("hold cycle needs at least two modes");
        QTest::newRow("off on a model without off") << 3 << false << true << QStringLiteral("this model has no Off mode to cycle");
        QTest::newRow("adaptive on a model without adaptive")
            << 0x0A << true << false << QStringLiteral("this model has no Adaptive mode to cycle");
        // The mode count is checked before the model, so a lone Off bit gets the count reason on any model.
        QTest::newRow("one bit before the model check") << 1 << false << false << QStringLiteral("hold cycle needs at least two modes");
        QTest::newRow("anc and transparency on any model") << 6 << false << false << QString();
        QTest::newRow("all four on a pro 2") << 15 << true << true << QString();
    }

    void refusal_refusesBadMasks()
    {
        QFETCH(int, mask);
        QFETCH(bool, hasOff);
        QFETCH(bool, hasAdaptive);
        QFETCH(QString, expected);

        const auto spec = forVerb(u"holdmodes");
        QVERIFY(spec.has_value());
        QCOMPARE(refusal(*spec, Request{QString(), mask}, hasOff, hasAdaptive), expected);
    }

    void refusal_isEmptyForEveryOtherKind()
    {
        for (const Spec &row : table()) {
            if (row.kind == Kind::Mask) {
                continue;
            }
            QVERIFY2(refusal(row, Request{QStringLiteral("on"), 1}, false, false).isEmpty(), row.verb);
        }
    }

    void encodeSides_case_data()
    {
        QTest::addColumn<QString>("choice");
        QTest::addColumn<bool>("hasCurrent");
        QTest::addColumn<QByteArray>("current");
        QTest::addColumn<QByteArray>("expected");

        // d1 is the right bud, d2 the left: 05 01 is right Siri, left noise.
        QTest::newRow("left keeps right at siri") << QStringLiteral("left:noise") << true << QByteArray::fromHex("0501") << QByteArray::fromHex("05010000");
        QTest::newRow("right replaces siri") << QStringLiteral("right:noise") << true << QByteArray::fromHex("0501") << QByteArray::fromHex("01010000");
        QTest::newRow("left with no current") << QStringLiteral("left:noise") << false << QByteArray() << QByteArray::fromHex("01010000");
        QTest::newRow("right with no current") << QStringLiteral("right:noise") << false << QByteArray() << QByteArray::fromHex("01010000");
        QTest::newRow("left keeps a four byte echo") << QStringLiteral("left:noise") << true << QByteArray::fromHex("05050000") << QByteArray::fromHex("05010000");
        QTest::newRow("right keeps trailing bytes") << QStringLiteral("right:noise") << true << QByteArray::fromHex("05050102") << QByteArray::fromHex("01050102");
    }

    void encodeSides_case()
    {
        QFETCH(QString, choice);
        QFETCH(bool, hasCurrent);
        QFETCH(QByteArray, current);
        QFETCH(QByteArray, expected);

        const std::optional<QByteArray> currentBytes = hasCurrent ? std::optional<QByteArray>(current) : std::nullopt;
        QCOMPARE(encodeSides(Request{choice, 0}, currentBytes).toHex(), expected.toHex());
    }

    void decode_case_data()
    {
        QTest::addColumn<QString>("verb");
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<QJsonValue>("expected");

        QTest::newRow("bool on") << QStringLiteral("allowoff") << QByteArray::fromHex("01000000") << QJsonValue(true);
        QTest::newRow("bool off") << QStringLiteral("allowoff") << QByteArray::fromHex("02000000") << QJsonValue(false);
        QTest::newRow("bool foreign byte") << QStringLiteral("allowoff") << QByteArray::fromHex("03000000") << QJsonValue();
        QTest::newRow("choice auto") << QStringLiteral("mic") << QByteArray::fromHex("00000000") << QJsonValue(QStringLiteral("auto"));
        QTest::newRow("choice left") << QStringLiteral("mic") << QByteArray::fromHex("02000000") << QJsonValue(QStringLiteral("left"));
        QTest::newRow("choice unknown byte") << QStringLiteral("mic") << QByteArray::fromHex("03000000") << QJsonValue(QStringLiteral("unknown"));
        QTest::newRow("level") << QStringLiteral("tone") << QByteArray::fromHex("28000000") << QJsonValue(40);
        QTest::newRow("mask") << QStringLiteral("holdmodes") << QByteArray::fromHex("0f000000") << QJsonValue(15);
        QTest::newRow("sides gives hold_left") << QStringLiteral("hold") << QByteArray::fromHex("0501") << QJsonValue(QStringLiteral("noise"));
        QTest::newRow("empty payload") << QStringLiteral("tone") << QByteArray() << QJsonValue();
        // An eight-byte echo leaves a one-byte payload, which still carries d1.
        QTest::newRow("one byte payload") << QStringLiteral("allowoff") << QByteArray::fromHex("01") << QJsonValue(true);
    }

    void decode_case()
    {
        QFETCH(QString, verb);
        QFETCH(QByteArray, payload);
        QFETCH(QJsonValue, expected);

        const auto spec = forVerb(verb);
        QVERIFY2(spec.has_value(), qPrintable(verb));
        QCOMPARE(decode(*spec, payload), expected);
    }

    // Every value the verb table can request reads back as itself from the four bytes the pods would echo.
    void roundTrip_everyRequestableValue()
    {
        for (const Spec &row : table()) {
            switch (row.kind) {
            case Kind::Bool:
                QCOMPARE(decode(row, encode(row, Request{QStringLiteral("on"), 0})), QJsonValue(true));
                QCOMPARE(decode(row, encode(row, Request{QStringLiteral("off"), 0})), QJsonValue(false));
                break;
            case Kind::Choice: {
                const QStringList names = QString::fromLatin1(row.choices).split(OpenPods::PodSettings::choiceSeparator);
                for (int index = 0; index < names.size(); ++index) {
                    QCOMPARE(decode(row, encode(row, Request{names.at(index), index})), QJsonValue(names.at(index)));
                }
                break;
            }
            case Kind::Level:
            case Kind::Mask:
                for (int value = row.min; value <= row.max; ++value) {
                    QCOMPARE(decode(row, encode(row, Request{QString(), value})), QJsonValue(value));
                }
                break;
            case Kind::Sides: {
                const QJsonObject sides = decodeSides(encodeSides(Request{QStringLiteral("right:noise"), 0}, QByteArray::fromHex("05050000")));
                QCOMPARE(sides.value(QStringLiteral("hold_right")), QJsonValue(QStringLiteral("noise")));
                QCOMPARE(sides.value(QStringLiteral("hold_left")), QJsonValue(QStringLiteral("siri")));
                break;
            }
            }
        }
    }

    void decodeSides_case_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");

        QTest::newRow("right siri left noise") << QByteArray::fromHex("05010000") << QStringLiteral("noise") << QStringLiteral("siri");
        QTest::newRow("both noise") << QByteArray::fromHex("01010000") << QStringLiteral("noise") << QStringLiteral("noise");
        QTest::newRow("left siri right noise") << QByteArray::fromHex("01050000") << QStringLiteral("siri") << QStringLiteral("noise");
        QTest::newRow("foreign bytes") << QByteArray::fromHex("00030000") << QStringLiteral("unknown") << QStringLiteral("unknown");
        QTest::newRow("empty payload") << QByteArray() << QStringLiteral("unknown") << QStringLiteral("unknown");
        QTest::newRow("one byte payload") << QByteArray::fromHex("05") << QStringLiteral("unknown") << QStringLiteral("siri");
    }

    void decodeSides_case()
    {
        QFETCH(QByteArray, payload);
        QFETCH(QString, left);
        QFETCH(QString, right);

        const QJsonObject sides = decodeSides(payload);
        QCOMPARE(sides.size(), 2);
        QCOMPARE(sides.value(QStringLiteral("hold_left")), QJsonValue(left));
        QCOMPARE(sides.value(QStringLiteral("hold_right")), QJsonValue(right));
    }

    void parseEq_case_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("accepted");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<int>("low");
        QTest::addColumn<int>("mid");
        QTest::addColumn<int>("high");

        QTest::newRow("on:50:50:50") << QStringLiteral("on:50:50:50") << true << true << 50 << 50 << 50;
        QTest::newRow("off:0:100:7") << QStringLiteral("off:0:100:7") << true << false << 0 << 100 << 7;
        QTest::newRow("band above 100") << QStringLiteral("on:101:0:0") << false << false << 0 << 0 << 0;
        QTest::newRow("band below 0") << QStringLiteral("on:-1:0:0") << false << false << 0 << 0 << 0;
        QTest::newRow("missing band") << QStringLiteral("on:1:2") << false << false << 0 << 0 << 0;
        QTest::newRow("extra band") << QStringLiteral("on:1:2:3:4") << false << false << 0 << 0 << 0;
        QTest::newRow("state only") << QStringLiteral("off") << false << false << 0 << 0 << 0;
        QTest::newRow("empty") << QString() << false << false << 0 << 0 << 0;
        QTest::newRow("foreign state") << QStringLiteral("maybe:1:2:3") << false << false << 0 << 0 << 0;
        QTest::newRow("leading space") << QStringLiteral("on: 1:2:3") << false << false << 0 << 0 << 0;
        QTest::newRow("not a number") << QStringLiteral("on:a:0:0") << false << false << 0 << 0 << 0;
        QTest::newRow("empty band") << QStringLiteral("on::0:0") << false << false << 0 << 0 << 0;
    }

    void parseEq_case()
    {
        QFETCH(QString, text);
        QFETCH(bool, accepted);
        QFETCH(bool, enabled);
        QFETCH(int, low);
        QFETCH(int, mid);
        QFETCH(int, high);

        const auto eq = parseEq(text);
        QCOMPARE(eq.has_value(), accepted);
        if (!accepted) {
            return;
        }
        QCOMPARE(eq->enabled, enabled);
        QCOMPARE(eq->low, low);
        QCOMPARE(eq->mid, mid);
        QCOMPARE(eq->high, high);
    }

    void eqJson_publishesFourKeys()
    {
        const QJsonObject object = eqJson(EqRequest{true, 50, 60, 70});
        QCOMPARE(object.size(), 4);
        QCOMPARE(object.value(QStringLiteral("enabled")), QJsonValue(true));
        QCOMPARE(object.value(QStringLiteral("low")), QJsonValue(50));
        QCOMPARE(object.value(QStringLiteral("mid")), QJsonValue(60));
        QCOMPARE(object.value(QStringLiteral("high")), QJsonValue(70));
        QCOMPARE(OpenPods::PodSettings::eqStatusKey, QStringLiteral("custom_eq"));
    }

    void customEqPacket()
    {
        QCOMPARE(AirPodsPackets::CustomEq::getPacket(true, 50, 50, 50).value_or(QByteArray()).toHex(),
                 QByteArray("04000400630005000102323232"));
        QCOMPARE(AirPodsPackets::CustomEq::getPacket(false, 0, 100, 7).value_or(QByteArray()).toHex(),
                 QByteArray("04000400630005000101006407"));
        QVERIFY(!AirPodsPackets::CustomEq::getPacket(true, 101, 0, 0).has_value());
        QVERIFY(!AirPodsPackets::CustomEq::getPacket(true, 0, -1, 0).has_value());
        QVERIFY(!AirPodsPackets::CustomEq::getPacket(true, 0, 0, 101).has_value());

        // parseEq output feeds the packet builder without any translation in between.
        const auto eq = parseEq(u"on:50:50:50");
        QVERIFY(eq.has_value());
        QCOMPARE(AirPodsPackets::CustomEq::getPacket(eq->enabled, eq->low, eq->mid, eq->high).value_or(QByteArray()).toHex(),
                 QByteArray("04000400630005000102323232"));
    }

    void renamePacket()
    {
        QVERIFY(!AirPodsPackets::Rename::getPacket(QString()).has_value());
        QVERIFY(!AirPodsPackets::Rename::getPacket(QString(33, QLatin1Char('a'))).has_value());

        // Sixteen two-byte characters is exactly 32 bytes; seventeen is 34 and must be refused, not truncated by the size byte.
        const QChar eAcute(0xE9);
        const QString widest(16, eAcute);
        QCOMPARE(widest.toUtf8().size(), 32);
        const auto packet = AirPodsPackets::Rename::getPacket(widest);
        QVERIFY(packet.has_value());
        QCOMPARE(*packet, QByteArray::fromHex("040004001A00012000") + widest.toUtf8());
        QVERIFY(!AirPodsPackets::Rename::getPacket(QString(17, eAcute)).has_value());

        QCOMPARE(AirPodsPackets::Rename::getPacket(QStringLiteral("Bryce's Pods")).value_or(QByteArray()),
                 QByteArray::fromHex("040004001A00010C00") + QByteArray("Bryce's Pods"));
    }

    void notificationMasks()
    {
        QCOMPARE(AirPodsPackets::Connection::notificationMaskDefault.size(), 5);
        QCOMPARE(AirPodsPackets::Connection::REQUEST_NOTIFICATIONS,
                 QByteArray::fromHex("040004000f00") + AirPodsPackets::Connection::notificationMaskDefault);
        QCOMPARE(AirPodsPackets::Connection::REQUEST_NOTIFICATIONS_EAR_DETECTION_OFF.toHex(), QByteArray("040004000f00fffffdffff"));

        // The two frames differ in exactly one byte, the third of the five mask bytes.
        const QByteArray &on = AirPodsPackets::Connection::REQUEST_NOTIFICATIONS;
        const QByteArray &off = AirPodsPackets::Connection::REQUEST_NOTIFICATIONS_EAR_DETECTION_OFF;
        QCOMPARE(on.size(), off.size());
        int differences = 0;
        for (int index = 0; index < on.size(); ++index) {
            if (on.at(index) != off.at(index)) {
                ++differences;
                QCOMPARE(index, 8);
            }
        }
        QCOMPARE(differences, 1);
    }
};

QTEST_GUILESS_MAIN(TestPodSettings)
#include "tst_podsettings.moc"
