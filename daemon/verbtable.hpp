#pragma once

#include "ipcverb.hpp"

#include <QByteArray>
#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <optional>

namespace OpenPods::Ipc
{
    enum class Kind
    {
        Bare,
        Bool,
        Choice,
        Int,
        Text
    };

    // verb is always the family; choices is the "a|b|c" list a Choice row accepts after the colon and stays null for every other kind.
    struct VerbSpec
    {
        const char *verb;
        Kind kind;
        const char *choices;
        int min;
        int max;
        const char *description;
    };

    // reply holds the exact bytes to write back when ok is false, and stays empty otherwise.
    struct Parsed
    {
        bool ok = false;
        QString verb;
        QString choice;
        // The Int payload, or the matched choice's index in its row's list.
        int number = 0;
        QString text;
        QByteArray reply;
    };

    inline constexpr QChar verbSeparator = u':';
    inline constexpr QChar choiceSeparator = u'|';
    // Every Bool row accepts exactly this list, which is why a Bool row carries no choices of its own.
    inline constexpr QStringView boolChoices = u"on|off";
    inline constexpr int adaptiveLevelMin = 0;
    inline constexpr int adaptiveLevelMax = 100;
    inline constexpr int usageVerbColumns = 24;
    inline constexpr int holdCycleMaskMin = 1;
    inline constexpr int holdCycleMaskMax = 15;
    inline constexpr int toneVolumeMin = 0;
    inline constexpr int toneVolumeMax = 100;
    inline constexpr int textMinBytes = 1;
    // The pods take at most 32 UTF-8 bytes of name, which AirPodsPackets::Rename::renameMaxBytes enforces again at the packet.
    inline constexpr int renameMaxBytes = 32;
    // Wider than the 14 bytes on:100:100:100 needs, so a bad band is refused by parseEq with its own reason rather than by the byte count.
    inline constexpr int eqMaxBytes = 32;

    // Adding a row here is the whole change for a new verb; the parser reads only this table.
    inline const QList<VerbSpec> &verbTable()
    {
        static const QList<VerbSpec> table = {
            {"reopen", Kind::Bare, nullptr, 0, 0, "Reopen the daemon window (a headless daemon refuses)"},
            {"status", Kind::Bare, nullptr, 0, 0, "Print one-line JSON status snapshot to stdout"},
            {"forget", Kind::Bare, nullptr, 0, 0, "Run `bluetoothctl remove` on the connected device"},
            {"disconnect", Kind::Bare, nullptr, 0, 0, "bluetoothctl disconnect on the paired AirPods"},
            {"connect", Kind::Bare, nullptr, 0, 0, "bluetoothctl connect on the paired AirPods"},
            // The first four choices sit in NoiseControlMode value order so number feeds setNoiseControlModeInt; cycle is last.
            {"noise", Kind::Choice, "off|anc|transparency|adaptive|cycle", 0, 0,
             "Set noise control: off, anc, transparency, adaptive, or cycle (Off->ANC->Trans->Adaptive)"},
            {"ear", Kind::Choice, "off|one|both", 0, 0,
             "Ear-detection auto-pause: off, one (pause when either pod is removed, default), both (only when both are)"},
            {"ca", Kind::Bool, nullptr, 0, 0, "Conversation Awareness (Pro2 only)"},
            {"onebud", Kind::Bool, nullptr, 0, 0, "One-Bud ANC (Pro2+: keep ANC active with only one pod in)"},
            {"adaptive", Kind::Int, nullptr, adaptiveLevelMin, adaptiveLevelMax,
             "Set Adaptive Noise level 0-100 (Pro2/Pro3, only while noise_mode=Adaptive)"},
            // Pod settings mirror OpenPods::PodSettings::table() row for row and description for description; tst_podsettings pins the two together.
            {"allowoff", Kind::Bool, nullptr, 0, 0, "Allow Off in the noise control cycle"},
            {"holdmodes", Kind::Int, nullptr, holdCycleMaskMin, holdCycleMaskMax,
             "Modes the stem hold cycles, as a bitmask: 1 Off, 2 ANC, 4 Transparency, 8 Adaptive (at least two)"},
            {"hold", Kind::Choice, "left:noise|right:noise", 0, 0,
             "Stem hold per bud: left:noise or right:noise (the other bud keeps its setting)"},
            {"mic", Kind::Choice, "auto|right|left", 0, 0, "Microphone: auto, right or left"},
            {"eardetect", Kind::Bool, nullptr, 0, 0, "Automatic ear detection on the buds"},
            {"swipe", Kind::Bool, nullptr, 0, 0, "Volume swipe on the stem"},
            {"swipespeed", Kind::Choice, "default|longer|longest", 0, 0, "Volume swipe length: default, longer or longest"},
            {"pvol", Kind::Bool, nullptr, 0, 0, "Personalized Volume"},
            {"tone", Kind::Int, nullptr, toneVolumeMin, toneVolumeMax, "Tone volume 0-100"},
            {"pressspeed", Kind::Choice, "default|slower|slowest", 0, 0, "Press speed: default, slower or slowest"},
            {"holdduration", Kind::Choice, "default|shorter|shortest", 0, 0, "Press and hold duration: default, shorter or shortest"},
            {"casetone", Kind::Bool, nullptr, 0, 0, "Charging case sounds"},
            {"sleep", Kind::Bool, nullptr, 0, 0, "Sleep detection: pause audio when you fall asleep"},
            {"autoconnect", Kind::Bool, nullptr, 0, 0, "Connect to this computer automatically"},
            {"allowautoconnect", Kind::Bool, nullptr, 0, 0, "Allow automatic connection"},
            {"rename", Kind::Text, nullptr, textMinBytes, renameMaxBytes, "Rename the AirPods (1 to 32 UTF-8 bytes)"},
            {"notify", Kind::Choice, "on|off|connected:on|connected:off", 0, 0,
             "Desktop toasts: on|off for all of them, connected:on|off for the battery banner on connect"},
            {"follow", Kind::Bool, nullptr, 0, 0, "Make the AirPods the default output when they connect"},
            {"eq", Kind::Text, nullptr, textMinBytes, eqMaxBytes, "Custom EQ: on|off:low:mid:high, each 0-100"},
        };
        return table;
    }

    // Sample input: "noise:anc" against the noise row, "adaptive:50" against the adaptive row; nullopt means the message is not addressed to this row.
    // Public only so the tests can reach Kind::Text before the rename row lands; the daemon calls parseVerb.
    inline std::optional<Parsed> matchVerb(const VerbSpec &row, QStringView message)
    {
        Parsed parsed;
        parsed.verb = QString::fromLatin1(row.verb);
        if (row.kind == Kind::Bare) {
            if (message != parsed.verb) {
                return std::nullopt;
            }
            parsed.ok = true;
            return parsed;
        }
        const QString prefix = parsed.verb + verbSeparator;
        if (!message.startsWith(prefix)) {
            return std::nullopt;
        }
        const QStringView payload = message.mid(prefix.size());
        switch (row.kind) {
        case Kind::Bool:
            if (boolChoices.split(choiceSeparator).contains(payload)) {
                parsed.ok = true;
                parsed.choice = payload.toString();
            } else {
                parsed.reply = QStringLiteral("error: %1 needs on or off\n").arg(parsed.verb).toUtf8();
            }
            break;
        case Kind::Choice: {
            const QString choices = QString::fromLatin1(row.choices);
            const qsizetype index = choices.split(choiceSeparator).indexOf(payload);
            if (index >= 0) {
                parsed.ok = true;
                parsed.choice = payload.toString();
                parsed.number = static_cast<int>(index);
            } else {
                parsed.reply = QStringLiteral("error: %1 needs one of %2\n").arg(parsed.verb, choices).toUtf8();
            }
            break;
        }
        case Kind::Int:
            if (const auto value = parseIntVerb(message, prefix, row.min, row.max)) {
                parsed.ok = true;
                parsed.number = *value;
            } else {
                parsed.reply = QStringLiteral("error: %1 needs a value %2..%3\n")
                                   .arg(parsed.verb, QString::number(row.min), QString::number(row.max))
                                   .toUtf8();
            }
            break;
        case Kind::Text: {
            const qsizetype bytes = payload.toUtf8().size();
            if (bytes >= row.min && bytes <= row.max) {
                parsed.ok = true;
                parsed.text = payload.toString();
            } else {
                parsed.reply = QStringLiteral("error: %1 needs %2..%3 bytes of text\n")
                                   .arg(parsed.verb, QString::number(row.min), QString::number(row.max))
                                   .toUtf8();
            }
            break;
        }
        case Kind::Bare:
            break;
        }
        return parsed;
    }

    // Sample input: "status", "noise:anc", "adaptive:50"; matched whole, so "adaptive: 5", "noise" and "" all fail.
    inline Parsed parseVerb(QStringView message)
    {
        for (const VerbSpec &row : verbTable()) {
            if (const auto parsed = matchVerb(row, message)) {
                return *parsed;
            }
        }
        Parsed unknown;
        unknown.reply = QStringLiteral("error: unknown verb %1\n").arg(message).toUtf8();
        return unknown;
    }

    // A Choice row shows a placeholder and names its values in the description, the way adaptive:N names 0-100, so every verb fits the column.
    inline QString usageForm(const VerbSpec &row)
    {
        const QString family = QString::fromLatin1(row.verb);
        switch (row.kind) {
        case Kind::Bare:
            return family;
        case Kind::Bool:
            return family + verbSeparator + boolChoices.toString();
        case Kind::Choice:
            return family + verbSeparator + QStringLiteral("MODE");
        case Kind::Int:
            return family + verbSeparator + QStringLiteral("N");
        case Kind::Text:
            return family + verbSeparator + QStringLiteral("TEXT");
        }
        return family;
    }

    // One line per row in the librepods-ctl usage shape: two spaces, the verb padded to 24 columns, the description.
    inline QStringList usageLines()
    {
        QStringList lines;
        for (const VerbSpec &row : verbTable()) {
            lines << QStringLiteral("  ") + usageForm(row).leftJustified(usageVerbColumns)
                         + QString::fromLatin1(row.description);
        }
        return lines;
    }
}
