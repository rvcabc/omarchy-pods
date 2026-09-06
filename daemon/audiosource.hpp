#pragma once

#include <QByteArray>
#include <QString>
#include <algorithm>
#include <optional>

// Opcode 0x0E tells every host which device holds the pods' audio right now, which is what Apple's automatic switching rides on.
namespace OpenPods::AudioSource
{
    enum class Type : quint8
    {
        None = 0x00,
        Call = 0x01,
        Media = 0x02,
        Unknown = 0xFF,
    };

    inline const QByteArray HEADER = QByteArray::fromHex("040004000e");
    // Header, one length byte, six MAC bytes, one type byte.
    inline constexpr int frameBytes = 13;
    inline constexpr int macIndex = 6;
    inline constexpr int macBytes = 6;
    inline constexpr int typeIndex = 12;

    struct Info
    {
        QByteArray deviceMac;
        Type type = Type::Unknown;
    };

    // Sample input: 04 00 04 00 0e 07 d8 2e f2 15 ef 0c 02 (this box, playing media); the MAC is byte-reversed on the wire.
    inline std::optional<Info> parse(const QByteArray &frame)
    {
        if (frame.size() < frameBytes || !frame.startsWith(HEADER)) {
            return std::nullopt;
        }
        Info info;
        info.deviceMac = frame.mid(macIndex, macBytes);
        switch (static_cast<quint8>(frame.at(typeIndex))) {
        case static_cast<quint8>(Type::None): info.type = Type::None; break;
        case static_cast<quint8>(Type::Call): info.type = Type::Call; break;
        case static_cast<quint8>(Type::Media): info.type = Type::Media; break;
        default: info.type = Type::Unknown; break;
        }
        return info;
    }

    // Sample input: "0C:EF:15:F2:2E:D8" -> d8 2e f2 15 ef 0c, the order the frame carries.
    inline QByteArray reversedMac(const QString &colonSeparated)
    {
        QByteArray bytes = QByteArray::fromHex(QString(colonSeparated).remove(QLatin1Char(':')).toLatin1());
        std::reverse(bytes.begin(), bytes.end());
        return bytes;
    }

    inline QString typeName(Type type)
    {
        switch (type) {
        case Type::None: return QStringLiteral("none");
        case Type::Call: return QStringLiteral("call");
        case Type::Media: return QStringLiteral("media");
        case Type::Unknown: break;
        }
        return QStringLiteral("unknown");
    }

    // The journal never carries a whole MAC, so the last two bytes stand for the device.
    inline QString macTail(const QByteArray &mac)
    {
        if (mac.size() < 2) {
            return QStringLiteral("??:??");
        }
        return QString::fromLatin1(mac.right(2).toHex(':'));
    }
}
