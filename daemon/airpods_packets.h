// airpods_packets.h
#ifndef AIRPODS_PACKETS_H
#define AIRPODS_PACKETS_H

#include <QByteArray>
#include <optional>
#include <climits>
#include <initializer_list>

#include "enums.h"
#include "BasicControlCommand.hpp"

namespace AirPodsPackets
{
    // Noise Control Mode Packets
    namespace NoiseControl
    {
        using NoiseControlMode = AirpodsTrayApp::Enums::NoiseControlMode;
        static const QByteArray HEADER = ControlCommand::HEADER + 0x0D;
        static const QByteArray OFF = ControlCommand::createCommand(0x0D, 0x01);
        static const QByteArray NOISE_CANCELLATION = ControlCommand::createCommand(0x0D, 0x02);
        static const QByteArray TRANSPARENCY = ControlCommand::createCommand(0x0D, 0x03);
        static const QByteArray ADAPTIVE = ControlCommand::createCommand(0x0D, 0x04);

        static const QByteArray getPacketForMode(AirpodsTrayApp::Enums::NoiseControlMode mode)
        {
            switch (mode)
            {
            case NoiseControlMode::Off:
                return OFF;
            case NoiseControlMode::NoiseCancellation:
                return NOISE_CANCELLATION;
            case NoiseControlMode::Transparency:
                return TRANSPARENCY;
            case NoiseControlMode::Adaptive:
                return ADAPTIVE;
            default:
                return QByteArray();
            }
        }

        inline std::optional<NoiseControlMode> parseMode(const QByteArray &data)
        {
            char mode = ControlCommand::parseActive(data).value_or(CHAR_MAX) - 1;
            if (mode < static_cast<quint8>(NoiseControlMode::MinValue) ||
                mode > static_cast<quint8>(NoiseControlMode::MaxValue))
            {
                return std::nullopt;
            }
            return static_cast<NoiseControlMode>(mode);
        }
    }

    // One Bud ANC Mode
    namespace OneBudANCMode
    {
        using Type = BasicControlCommand<0x1B>;
        static const QByteArray ENABLED = Type::ENABLED;
        static const QByteArray DISABLED = Type::DISABLED;
        static const QByteArray HEADER = Type::HEADER;
        inline std::optional<bool> parseState(const QByteArray &data) { return Type::parseState(data); }
    }

    // Volume Swipe (partial - still needs custom interval function)
    namespace VolumeSwipe
    {
        using Type = BasicControlCommand<0x25>;
        static const QByteArray ENABLED = Type::ENABLED;
        static const QByteArray DISABLED = Type::DISABLED;
        static const QByteArray HEADER = Type::HEADER;
        inline std::optional<bool> parseState(const QByteArray &data) { return Type::parseState(data); }

        // Keep custom interval function
        static QByteArray getIntervalPacket(quint8 interval)
        {
            return ControlCommand::createCommand(0x23, interval);
        }
    }

    // Adaptive Volume Config
    namespace AdaptiveVolume
    {
        using Type = BasicControlCommand<0x26>;
        static const QByteArray ENABLED = Type::ENABLED;
        static const QByteArray DISABLED = Type::DISABLED;
        static const QByteArray HEADER = Type::HEADER;
        inline std::optional<bool> parseState(const QByteArray &data) { return Type::parseState(data); }
    }

    // Conversational Awareness
    namespace ConversationalAwareness
    {
        using Type = BasicControlCommand<0x28>;
        static const QByteArray ENABLED = Type::ENABLED;
        static const QByteArray DISABLED = Type::DISABLED;
        static const QByteArray HEADER = Type::HEADER;
        static const QByteArray DATA_HEADER = QByteArray::fromHex("040004004B00020001");
        inline std::optional<bool> parseState(const QByteArray &data) { return Type::parseState(data); }
    }

    // Hearing Assist
    namespace HearingAssist
    {
        using Type = BasicControlCommand<0x33>;
        static const QByteArray ENABLED = Type::ENABLED;
        static const QByteArray DISABLED = Type::DISABLED;
        static const QByteArray HEADER = Type::HEADER;
        inline std::optional<bool> parseState(const QByteArray &data) { return Type::parseState(data); }
    }

    // Hearing Aid
    namespace HearingAid
    {
        static const QByteArray HEADER = ControlCommand::HEADER + static_cast<char>(0x2C);
        static const QByteArray ENABLED = ControlCommand::createCommand(0x2C, 0x01, 0x01);
        static const QByteArray DISABLED = ControlCommand::createCommand(0x2C, 0x02, 0x02);

        inline std::optional<bool> parseState(const QByteArray &data)
        {
            if (!data.startsWith(HEADER) || data.size() < HEADER.size() + 2)
                return std::nullopt;

            QByteArray value = data.mid(HEADER.size(), 2);
            if (value.size() != 2)
                return std::nullopt;

            char b1 = value.at(0);
            char b2 = value.at(1);

            if (b1 == 0x01 && b2 == 0x01)
                return true;
            if (b1 == 0x02 || b2 == 0x02)
                return false;

            return std::nullopt;
        }
    }

    // Allow Off Option
    namespace AllowOffOption
    {
        using Type = BasicControlCommand<0x34>;
        static const QByteArray ENABLED = Type::ENABLED;
        static const QByteArray DISABLED = Type::DISABLED;
        static const QByteArray HEADER = Type::HEADER;
        inline std::optional<bool> parseState(const QByteArray &data) { return Type::parseState(data); }
    }

    // Connection Packets
    namespace Connection
    {
        static const QByteArray HANDSHAKE = QByteArray::fromHex("00000400010002000000000000000000");
        static const QByteArray SET_SPECIFIC_FEATURES = QByteArray::fromHex("040004004d00d700000000000000");
        static const QByteArray REQUEST_NOTIFICATIONS = QByteArray::fromHex("040004000f00ffffffffff");
        // The fork sends five mask bytes where Android sends four; the ear-detection bit's place is proven on hardware by the eardetect:off check.
        static const QByteArray notificationMaskDefault = QByteArray::fromHex("ffffffffff");
        static const QByteArray REQUEST_NOTIFICATIONS_EAR_DETECTION_OFF = QByteArray::fromHex("040004000f00fffffdffff");
        static const QByteArray AIRPODS_DISCONNECTED = QByteArray::fromHex("00010000");
    }

    // Phone Communication Packets
    namespace Phone
    {
        static const QByteArray NOTIFICATION = QByteArray::fromHex("00040001");
        static const QByteArray CONNECTED = QByteArray::fromHex("00010001");
        static const QByteArray DISCONNECTED = QByteArray::fromHex("00010000");
        static const QByteArray STATUS_REQUEST = QByteArray::fromHex("00020003");
        static const QByteArray DISCONNECT_REQUEST = QByteArray::fromHex("00020000");
    }

    // Adaptive Noise Packets
    namespace AdaptiveNoise
    {
        const QByteArray HEADER = QByteArray::fromHex("0400040009002E");

        inline QByteArray getPacket(int level)
        {
            return HEADER + static_cast<char>(level) + QByteArray::fromHex("000000");
        }
    }

    namespace Rename
    {
        // One byte carries the length, so an empty name or one over the 32 bytes the rename verb allows is refused here rather than truncated.
        inline constexpr int renameMaxBytes = 32;

        // Sample input: "Bryce's Pods" -> 04 00 04 00 1A 00 01 0C 00 42 72 79 63 65 27 73 20 50 6F 64 73; nullopt for 0 or more than 32 UTF-8 bytes.
        inline std::optional<QByteArray> getPacket(const QString &newName)
        {
            const QByteArray nameBytes = newName.toUtf8();
            if (nameBytes.isEmpty() || nameBytes.size() > renameMaxBytes) {
                return std::nullopt;
            }
            QByteArray packet = QByteArray::fromHex("040004001A0001");
            packet.append(static_cast<char>(nameBytes.size()));
            packet.append('\0');
            packet.append(nameBytes);
            return packet;
        }
    }

    // Custom EQ is opcode 0x63, not a control command: 04 00 04 00 63 00 05 00 01 [state] [low] [mid] [high].
    namespace CustomEq
    {
        inline const QByteArray HEADER = QByteArray::fromHex("040004006300050001");
        // Unlike a control command, 0x02 here means enabled and 0x01 disabled.
        inline constexpr quint8 enabledState = 0x02;
        inline constexpr quint8 disabledState = 0x01;
        inline constexpr int bandMin = 0;
        inline constexpr int bandMax = 100;

        // Sample input: enabled, 50, 50, 50 -> 04 00 04 00 63 00 05 00 01 02 32 32 32; a band outside 0..100 is nullopt.
        inline std::optional<QByteArray> getPacket(bool enabled, int low, int mid, int high)
        {
            for (const int band : {low, mid, high}) {
                if (band < bandMin || band > bandMax) {
                    return std::nullopt;
                }
            }
            QByteArray packet = HEADER;
            packet.append(static_cast<char>(enabled ? enabledState : disabledState));
            packet.append(static_cast<char>(low));
            packet.append(static_cast<char>(mid));
            packet.append(static_cast<char>(high));
            return packet;
        }
    }

    namespace MagicPairing {
        static const QByteArray REQUEST_MAGIC_CLOUD_KEYS = QByteArray::fromHex("0400040030000500");
        static const QByteArray MAGIC_CLOUD_KEYS_HEADER = QByteArray::fromHex("04000400310002");

        struct MagicCloudKeys {
            QByteArray magicAccIRK;      // 16 bytes
            QByteArray magicAccEncKey;    // 16 bytes
        };

        inline MagicCloudKeys parseMagicCloudKeysPacket(const QByteArray &data)
        {
            MagicCloudKeys keys;

            if (data.size() < 47 || !data.startsWith(MAGIC_CLOUD_KEYS_HEADER))
            {
                return keys;
            }

            int index = MAGIC_CLOUD_KEYS_HEADER.size();

            // First TLV block (MagicAccIRK)
            if (static_cast<quint8>(data.at(index)) != 0x01)
                return keys;
            index += 1;

            quint16 len1 = (static_cast<quint8>(data.at(index)) << 8) | static_cast<quint8>(data.at(index + 1));
            if (len1 != 16)
                return keys;
            index += 3; // Skip length (2 bytes) and reserved byte (1 byte)

            keys.magicAccIRK = data.mid(index, 16);
            index += 16;

            // Second TLV block (MagicAccEncKey)
            if (static_cast<quint8>(data.at(index)) != 0x04)
                return keys;
            index += 1;

            quint16 len2 = (static_cast<quint8>(data.at(index)) << 8) | static_cast<quint8>(data.at(index + 1));
            if (len2 != 16)
                return keys;
            index += 3; // Skip length (2 bytes) and reserved byte (1 byte)

            keys.magicAccEncKey = data.mid(index, 16);

            return keys;
        }
    }

    // Parsing Headers
    namespace Parse
    {
        static const QByteArray EAR_DETECTION = QByteArray::fromHex("040004000600");
        static const QByteArray BATTERY_STATUS = QByteArray::fromHex("040004000400");
        static const QByteArray METADATA = QByteArray::fromHex("040004001d");
        static const QByteArray HANDSHAKE_ACK = QByteArray::fromHex("01000400");
        static const QByteArray FEATURES_ACK = QByteArray::fromHex("040004002b00"); // Note: Only tested with airpods pro 2
    }
}

#endif // AIRPODS_PACKETS_H
