// The sample frame here is synthetic; the real capture from Bryce's pods replaces it once its serials are scrubbed.
#pragma once

#include <QByteArray>
#include <QString>

#include "airpods_packets.h"

namespace OpenPods
{
// Byte 5 is the opcode high byte that Parse::METADATA alone leaves out of its header constant, bytes 6 to 10 are unidentified, and main.cpp has always skipped all six.
inline constexpr qsizetype metadataPrefixBytes = 6;

// Rename::getPacket carries the name length in one byte, so no string field can legitimately exceed 255 bytes.
inline constexpr qsizetype metadataMaxFieldBytes = 255;

// Upstream counts 13 NUL-terminated strings before the encrypted blobs but names only these eleven; the remainder is dropped until a real capture settles what it holds.
struct Metadata
{
    QString name;
    QString modelNumber;
    QString manufacturer;
    QString serialNumber;
    QString firmwareVersion;
    QString firmwareVersion2;
    QString hardwareRevision;
    QString updaterIdentifier;
    QString leftSerial;
    QString rightSerial;
    QString numericVersion;
};

// error is the text the caller logs when ok is false; the header stays free of logger.h so the suite can run it.
struct MetadataParse
{
    bool ok = false;
    QString error;
    Metadata value;
};

// Sample input: 04 00 04 00 1d 00 00 00 00 00 00 "AirPods Pro\0A2698\0Apple Inc.\0H1ABCDEFGHIJ\06E188\06E188\01.0.0\0com.apple.accessory.updater.app.71\0H1LEFT000001\0H1RIGHT00001\06E188\0" 01 02
inline MetadataParse parseMetadata(const QByteArray &frame)
{
    MetadataParse result;
    const QByteArray &header = AirPodsPackets::Parse::METADATA;
    if (!frame.startsWith(header)) {
        result.error = QStringLiteral("metadata frame: header %1 is not %2")
                           .arg(QString::fromLatin1(frame.left(header.size()).toHex()),
                                QString::fromLatin1(header.toHex()));
        return result;
    }

    const qsizetype stringsStart = header.size() + metadataPrefixBytes;
    if (frame.size() < stringsStart) {
        result.error = QStringLiteral("metadata frame: %1 bytes, header plus prefix needs %2")
                           .arg(frame.size())
                           .arg(stringsStart);
        return result;
    }

    struct Field
    {
        const char *name;
        QString *target;
    };
    const Field fields[] = {
        {"name", &result.value.name},
        {"modelNumber", &result.value.modelNumber},
        {"manufacturer", &result.value.manufacturer},
        {"serialNumber", &result.value.serialNumber},
        {"firmwareVersion", &result.value.firmwareVersion},
        {"firmwareVersion2", &result.value.firmwareVersion2},
        {"hardwareRevision", &result.value.hardwareRevision},
        {"updaterIdentifier", &result.value.updaterIdentifier},
        {"leftSerial", &result.value.leftSerial},
        {"rightSerial", &result.value.rightSerial},
        {"numericVersion", &result.value.numericVersion},
    };

    qsizetype pos = stringsStart;
    for (const Field &field : fields) {
        // Older firmware sends fewer strings, so a frame that ends on a string boundary leaves the remaining fields empty rather than failing.
        if (pos >= frame.size()) {
            break;
        }
        const qsizetype terminator = frame.indexOf('\0', pos);
        // A frame that ends inside a string was never whole, and the caller persists these fields, so a partial serial must not come back as a value.
        if (terminator < 0) {
            result.error = QStringLiteral("metadata frame: %1 runs %2 bytes to the end of the frame without NUL")
                               .arg(QString::fromLatin1(field.name))
                               .arg(frame.size() - pos);
            return result;
        }
        const qsizetype length = terminator - pos;
        if (length > metadataMaxFieldBytes) {
            result.error = QStringLiteral("metadata frame: %1 is %2 bytes, limit %3")
                               .arg(QString::fromLatin1(field.name))
                               .arg(length)
                               .arg(metadataMaxFieldBytes);
            return result;
        }
        *field.target = QString::fromUtf8(frame.constData() + pos, length);
        pos = terminator + 1;
    }

    result.ok = true;
    return result;
}
}
