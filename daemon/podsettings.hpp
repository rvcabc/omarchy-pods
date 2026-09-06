#pragma once

// One row per AirPods setting that macOS exposes as a plain control command, so main.cpp sets, persists, publishes and re-asserts all of them from this table alone.

#include "ipcverb.hpp"

#include <QByteArray>
#include <QChar>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QtAlgorithms>
#include <QtGlobal>
#include <optional>

namespace OpenPods::PodSettings
{
    // Choice rows send the choice index as d1 (mic: auto 00, right 01, left 02), not the 01/02 convention Bool rows use.
    enum class Kind
    {
        Bool,
        Choice,
        Level,
        Mask,
        Sides
    };

    // choices is the "a|b|c" list in wire order for Choice and Sides rows and stays null otherwise; min and max apply only to Level and Mask.
    struct Spec
    {
        const char *verb;
        quint8 id;
        const char *statusKey;
        Kind kind;
        const char *choices;
        int min;
        int max;
        const char *description;
    };

    // What the verb parser produced: choice for Bool, Choice and Sides rows, number for Level and Mask rows.
    struct Request
    {
        QString choice;
        int number;
    };

    // A control command always carries four data bytes on the wire; every single-byte kind lives in d1.
    inline constexpr int wireDataBytes = 4;
    inline constexpr int d1Index = 0;
    inline constexpr int d2Index = 1;
    inline constexpr quint8 boolOnByte = 0x01;
    inline constexpr quint8 boolOffByte = 0x02;
    inline constexpr QStringView boolOn = u"on";
    inline constexpr QStringView boolOff = u"off";
    inline constexpr QChar choiceSeparator = u'|';
    inline constexpr QStringView unknownChoice = u"unknown";
    inline constexpr int levelMin = 0;
    inline constexpr int levelMax = 100;

    // CC 0x1A: bit 1 Off, 2 ANC, 4 Transparency, 8 Adaptive, and the pods need at least two to cycle between.
    inline constexpr int holdCycleOffBit = 0x01;
    inline constexpr int holdCycleAdaptiveBit = 0x08;
    inline constexpr int holdCycleMaskMin = 0x01;
    inline constexpr int holdCycleMaskMax = 0x0F;
    inline constexpr int holdCycleMinModes = 2;

    // CC 0x16: d1 is the right bud and d2 the left; 0x01 is noise control and 0x05 is Siri.
    inline constexpr int holdRightIndex = d1Index;
    inline constexpr int holdLeftIndex = d2Index;
    inline constexpr quint8 holdNoiseByte = 0x01;
    inline constexpr quint8 holdSiriByte = 0x05;
    inline constexpr QStringView holdLeftChoice = u"left:noise";
    inline const QString holdLeftStatusKey = QStringLiteral("hold_left");
    inline const QString holdRightStatusKey = QStringLiteral("hold_right");
    inline const QString holdNoiseName = QStringLiteral("noise");
    inline const QString holdSiriName = QStringLiteral("siri");
    // Both buds at noise is the only shape the verb table can request, so it is also the start before any echo or saved value exists.
    inline const QByteArray holdBothNoise = QByteArray::fromHex("01010000");

    // Adding a row here is the whole change for a new setting, apart from mirroring it in verbTable.
    inline const QList<Spec> &table()
    {
        static const QList<Spec> rows = {
            {"allowoff", 0x34, "allow_off", Kind::Bool, nullptr, 0, 0, "Allow Off in the noise control cycle"},
            {"holdmodes", 0x1A, "hold_cycle_modes", Kind::Mask, nullptr, holdCycleMaskMin, holdCycleMaskMax,
             "Modes the stem hold cycles, as a bitmask: 1 Off, 2 ANC, 4 Transparency, 8 Adaptive (at least two)"},
            {"hold", 0x16, "hold_left", Kind::Sides, "left:noise|right:noise", 0, 0,
             "Stem hold per bud: left:noise or right:noise (the other bud keeps its setting)"},
            {"mic", 0x01, "mic_mode", Kind::Choice, "auto|right|left", 0, 0, "Microphone: auto, right or left"},
            {"eardetect", 0x0A, "ear_detection_on_bud", Kind::Bool, nullptr, 0, 0, "Automatic ear detection on the buds"},
            {"swipe", 0x25, "volume_swipe", Kind::Bool, nullptr, 0, 0, "Volume swipe on the stem"},
            {"swipespeed", 0x23, "volume_swipe_speed", Kind::Choice, "default|longer|longest", 0, 0,
             "Volume swipe length: default, longer or longest"},
            {"pvol", 0x26, "personalized_volume", Kind::Bool, nullptr, 0, 0, "Personalized Volume"},
            {"tone", 0x1F, "tone_volume", Kind::Level, nullptr, levelMin, levelMax, "Tone volume 0-100"},
            {"pressspeed", 0x17, "press_speed", Kind::Choice, "default|slower|slowest", 0, 0,
             "Press speed: default, slower or slowest"},
            {"holdduration", 0x18, "hold_duration", Kind::Choice, "default|shorter|shortest", 0, 0,
             "Press and hold duration: default, shorter or shortest"},
            {"casetone", 0x31, "case_sounds", Kind::Bool, nullptr, 0, 0, "Charging case sounds"},
            {"sleep", 0x35, "sleep_detection", Kind::Bool, nullptr, 0, 0, "Sleep detection: pause audio when you fall asleep"},
            {"autoconnect", 0x20, "connect_automatically", Kind::Bool, nullptr, 0, 0, "Connect to this computer automatically"},
            {"allowautoconnect", 0x36, "allow_auto_connect", Kind::Bool, nullptr, 0, 0, "Allow automatic connection"},
        };
        return rows;
    }

    inline std::optional<Spec> forVerb(QStringView verb)
    {
        for (const Spec &row : table()) {
            if (QLatin1StringView(row.verb) == verb) {
                return row;
            }
        }
        return std::nullopt;
    }

    inline std::optional<Spec> forId(quint8 id)
    {
        for (const Spec &row : table()) {
            if (row.id == id) {
                return row;
            }
        }
        return std::nullopt;
    }

    // Sample input: "left:noise" with current 05 01 00 00 (right Siri, left noise) gives 05 01 00 00, "right:noise" gives 01 01 00 00; nullopt current starts from 01 01 00 00.
    inline QByteArray encodeSides(const Request &request, const std::optional<QByteArray> &current)
    {
        QByteArray data = current.has_value() ? current->leftJustified(wireDataBytes, '\0', true) : holdBothNoise;
        // The row's choices are left:noise and right:noise, so anything that is not left is right.
        const int side = request.choice == holdLeftChoice ? holdLeftIndex : holdRightIndex;
        data[side] = static_cast<char>(holdNoiseByte);
        return data;
    }

    // Sample input: allowoff on -> 01 00 00 00, mic left -> 02 00 00 00, tone 40 -> 28 00 00 00, holdmodes 7 -> 07 00 00 00; the caller wraps these in ControlCommand::createCommand.
    inline QByteArray encode(const Spec &spec, const Request &request)
    {
        QByteArray data(wireDataBytes, '\0');
        switch (spec.kind) {
        case Kind::Bool:
            data[d1Index] = static_cast<char>(request.choice == boolOn ? boolOnByte : boolOffByte);
            break;
        case Kind::Choice:
        case Kind::Level:
        case Kind::Mask:
            // The verb parser already bounded number to a choice index, 0..100 or 1..15.
            data[d1Index] = static_cast<char>(request.number);
            break;
        case Kind::Sides:
            // Keeping the other bud needs the current bytes, so main.cpp calls encodeSides with them; without them both buds start at noise.
            return encodeSides(request, std::nullopt);
        }
        return data;
    }

    // Empty when acceptable; only a Mask row can be refused here because the verb parser already bounded every other kind.
    inline QString refusal(const Spec &spec, const Request &request, bool hasOff, bool hasAdaptive)
    {
        if (spec.kind != Kind::Mask) {
            return QString();
        }
        if (qPopulationCount(static_cast<quint32>(request.number)) < holdCycleMinModes) {
            return QStringLiteral("hold cycle needs at least two modes");
        }
        if ((request.number & holdCycleOffBit) && !hasOff) {
            return QStringLiteral("this model has no Off mode to cycle");
        }
        if ((request.number & holdCycleAdaptiveBit) && !hasAdaptive) {
            return QStringLiteral("this model has no Adaptive mode to cycle");
        }
        return QString();
    }

    // Sample input: 05 01 00 00 (right Siri, left noise) -> {"hold_left": "noise", "hold_right": "siri"}; a missing or foreign byte reads "unknown".
    inline QJsonObject decodeSides(const QByteArray &payload)
    {
        const auto name = [&payload](int index) {
            if (index >= payload.size()) {
                return QJsonValue(unknownChoice.toString());
            }
            const auto value = static_cast<quint8>(payload.at(index));
            if (value == holdNoiseByte) {
                return QJsonValue(holdNoiseName);
            }
            if (value == holdSiriByte) {
                return QJsonValue(holdSiriName);
            }
            return QJsonValue(unknownChoice.toString());
        };
        QJsonObject sides;
        sides.insert(holdLeftStatusKey, name(holdLeftIndex));
        sides.insert(holdRightStatusKey, name(holdRightIndex));
        return sides;
    }

    // Sample input: the bytes after the id, 01 00 00 00 on a Bool row -> true, 02 -> false, anything else -> null; a payload with no d1 is null for every kind.
    inline QJsonValue decode(const Spec &spec, const QByteArray &payload)
    {
        if (payload.isEmpty()) {
            return QJsonValue();
        }
        const auto value = static_cast<quint8>(payload.at(d1Index));
        switch (spec.kind) {
        case Kind::Bool:
            if (value == boolOnByte) {
                return true;
            }
            if (value == boolOffByte) {
                return false;
            }
            return QJsonValue();
        case Kind::Choice: {
            const QStringList names = QString::fromLatin1(spec.choices).split(choiceSeparator);
            return value < names.size() ? QJsonValue(names.at(value)) : QJsonValue(unknownChoice.toString());
        }
        case Kind::Level:
        case Kind::Mask:
            return static_cast<int>(value);
        case Kind::Sides:
            // Sides publishes two keys, so the generic path gets hold_left here and decodeSides supplies both.
            return decodeSides(payload).value(holdLeftStatusKey);
        }
        return QJsonValue();
    }

    // Custom EQ is opcode 0x63 rather than a control command, so it has its own request and packet builder (AirPodsPackets::CustomEq).
    struct EqRequest
    {
        bool enabled;
        int low;
        int mid;
        int high;
    };

    inline constexpr QChar eqSeparator = u':';
    inline constexpr int eqFieldCount = 4;
    inline constexpr int eqBandMin = 0;
    inline constexpr int eqBandMax = 100;
    inline const QString eqStatusKey = QStringLiteral("custom_eq");

    // Sample input: "on:50:50:50" or "off:50:50:50", the state then low, mid and high 0..100; anything else, including a missing band, is nullopt.
    inline std::optional<EqRequest> parseEq(QStringView text)
    {
        const QList<QStringView> fields = text.split(eqSeparator);
        if (fields.size() != eqFieldCount) {
            return std::nullopt;
        }
        if (fields.at(0) != boolOn && fields.at(0) != boolOff) {
            return std::nullopt;
        }
        // An empty prefix makes parseIntVerb a plain bounded integer parser that still refuses leading whitespace.
        const auto band = [](QStringView field) { return Ipc::parseIntVerb(field, QStringView(), eqBandMin, eqBandMax); };
        const auto low = band(fields.at(1));
        const auto mid = band(fields.at(2));
        const auto high = band(fields.at(3));
        if (!low || !mid || !high) {
            return std::nullopt;
        }
        return EqRequest{fields.at(0) == boolOn, *low, *mid, *high};
    }

    // The status.json value under custom_eq: {"enabled": true, "low": 50, "mid": 50, "high": 50}.
    inline QJsonObject eqJson(const EqRequest &eq)
    {
        QJsonObject object;
        object.insert(QStringLiteral("enabled"), eq.enabled);
        object.insert(QStringLiteral("low"), eq.low);
        object.insert(QStringLiteral("mid"), eq.mid);
        object.insert(QStringLiteral("high"), eq.high);
        return object;
    }
}
