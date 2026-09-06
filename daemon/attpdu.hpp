#pragma once

#include <QByteArray>
#include <QString>

// The hearing controls the pods keep behind ATT on L2CAP PSM 31, not on the AAP channel; the handles come from the
// upstream Python tool that drove them first (hearing-aid-adjustments.py).
namespace OpenPods::Att
{
    inline constexpr quint16 psm = 31;

    inline constexpr quint8 opcodeError = 0x01;
    inline constexpr quint8 opcodeReadRequest = 0x0A;
    inline constexpr quint8 opcodeReadResponse = 0x0B;
    inline constexpr quint8 opcodeWriteRequest = 0x12;
    inline constexpr quint8 opcodeWriteResponse = 0x13;
    inline constexpr quint8 opcodeNotification = 0x1B;

    inline constexpr quint16 transparencyHandle = 0x18;
    inline constexpr quint16 loudSoundReductionHandle = 0x1B;
    inline constexpr quint16 hearingAidHandle = 0x2A;
    // The client characteristic configuration sits one handle above its characteristic on these pods.
    inline constexpr quint16 cccdOffset = 1;

    // Loud Sound Reduction is a plain byte, and unlike a control command it is 0x01 on and 0x00 off.
    inline constexpr quint8 loudSoundReductionOn = 0x01;
    inline constexpr quint8 loudSoundReductionOff = 0x00;

    inline constexpr int requestTimeoutMs = 2000;
    inline constexpr int minReplyBytes = 1;
    inline constexpr int errorReplyBytes = 5;
    inline constexpr int handleBytes = 2;

    inline QByteArray handleBytesLittleEndian(quint16 handle)
    {
        QByteArray bytes;
        bytes.append(static_cast<char>(handle & 0xFF));
        bytes.append(static_cast<char>((handle >> 8) & 0xFF));
        return bytes;
    }

    // Sample output for handle 0x1B: 0a 1b 00
    inline QByteArray readRequest(quint16 handle)
    {
        return QByteArray(1, static_cast<char>(opcodeReadRequest)) + handleBytesLittleEndian(handle);
    }

    // Sample output for handle 0x1B, value 01: 12 1b 00 01
    inline QByteArray writeRequest(quint16 handle, const QByteArray &value)
    {
        return QByteArray(1, static_cast<char>(opcodeWriteRequest)) + handleBytesLittleEndian(handle) + value;
    }

    // Sample output for handle 0x2A: 12 2b 00 01 00 (notifications on the CCCD above it)
    inline QByteArray notificationsOn(quint16 handle)
    {
        return writeRequest(static_cast<quint16>(handle + cccdOffset), QByteArray::fromHex("0100"));
    }

    struct Reply
    {
        enum class Kind
        {
            ReadResponse,
            WriteResponse,
            Error,
            Notification,
            Unknown,
        };
        Kind kind = Kind::Unknown;
        // Filled for Error (the handle the request named) and Notification.
        quint16 handle = 0;
        quint8 requestOpcode = 0;
        quint8 errorCode = 0;
        QByteArray value;

        QString describe() const
        {
            switch (kind) {
            case Kind::ReadResponse: return QStringLiteral("ATT read response %1").arg(QString::fromLatin1(value.toHex()));
            case Kind::WriteResponse: return QStringLiteral("ATT write response");
            case Kind::Error: return QStringLiteral("ATT error 0x%1 for request 0x%2 on handle 0x%3")
                                         .arg(errorCode, 2, 16, QLatin1Char('0')).arg(requestOpcode, 2, 16, QLatin1Char('0')).arg(handle, 2, 16, QLatin1Char('0'));
            case Kind::Notification: return QStringLiteral("ATT notification on handle 0x%1: %2").arg(handle, 2, 16, QLatin1Char('0')).arg(QString::fromLatin1(value.toHex()));
            case Kind::Unknown: break;
            }
            return QStringLiteral("ATT unknown reply %1").arg(QString::fromLatin1(value.toHex()));
        }
    };

    // Sample inputs: 0b 01 (read answered 01), 13 (write answered), 01 12 1b 00 03 (write to 0x1B refused, code 3),
    // 1b 2a 00 aa bb (notification from 0x2A carrying aa bb).
    inline Reply parseReply(const QByteArray &pdu)
    {
        Reply reply;
        if (pdu.size() < minReplyBytes) {
            return reply;
        }
        const quint8 opcode = static_cast<quint8>(pdu.at(0));
        switch (opcode) {
        case opcodeReadResponse:
            reply.kind = Reply::Kind::ReadResponse;
            reply.value = pdu.mid(1);
            return reply;
        case opcodeWriteResponse:
            reply.kind = Reply::Kind::WriteResponse;
            return reply;
        case opcodeError:
            if (pdu.size() < errorReplyBytes) {
                reply.value = pdu;
                return reply;
            }
            reply.kind = Reply::Kind::Error;
            reply.requestOpcode = static_cast<quint8>(pdu.at(1));
            reply.handle = static_cast<quint16>(static_cast<quint8>(pdu.at(2)) | (static_cast<quint8>(pdu.at(3)) << 8));
            reply.errorCode = static_cast<quint8>(pdu.at(4));
            return reply;
        case opcodeNotification:
            if (pdu.size() < 1 + handleBytes) {
                reply.value = pdu;
                return reply;
            }
            reply.kind = Reply::Kind::Notification;
            reply.handle = static_cast<quint16>(static_cast<quint8>(pdu.at(1)) | (static_cast<quint8>(pdu.at(2)) << 8));
            reply.value = pdu.mid(1 + handleBytes);
            return reply;
        default:
            reply.value = pdu;
            return reply;
        }
    }
}
