#pragma once

#include "BasicControlCommand.hpp"
#include "airpods_packets.h"

#include <QByteArray>
#include <QChar>
#include <QList>
#include <QMetaType>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <algorithm>
#include <iterator>
#include <optional>

// Apple devices overwrite a few control-command settings on every connect, so the daemon keeps what the user asked for and sends it again after each one.
namespace OpenPods::Reassert
{
    // The listening-mode cycle bitmask has no entry in airpods_packets.h yet, so its id is named here.
    inline constexpr quint8 listeningModeCycleId = 0x1A;
    inline constexpr quint8 allowOffId = AirPodsPackets::AllowOffOption::Type::ID;

    // Only this allowlist is re-sent: 0x0D listening mode and 0x06 owns-connection echo back too, and re-sending those would fight the pods.
    inline constexpr quint8 stickyIds[] = { listeningModeCycleId, allowOffId };

    inline bool isSticky(quint8 id)
    {
        return std::find(std::begin(stickyIds), std::end(stickyIds), id) != std::end(stickyIds);
    }

    // Sent earlier than this, the setting is overwritten by the burst the pods send right after REQUEST_NOTIFICATIONS.
    inline constexpr int reassertAfterNotificationsMs = 3000;

    // A control command always carries four data bytes on the wire.
    inline constexpr int dataByteCount = 4;

    // The comparison and the log line both use the four bytes the wire carries, so a one-byte request lines up with its four-byte echo.
    inline QByteArray wireData(const QByteArray &payload)
    {
        return payload.leftJustified(dataByteCount, '\0', true);
    }

    inline const QString settingsGroup = QStringLiteral("PodSettings");

    // "PodSettings/1a": the id as two lowercase hex digits, so the ini file reads like the protocol notes.
    inline QString settingsKey(quint8 id)
    {
        return settingsGroup + QLatin1Char('/')
               + QString::number(id, 16).rightJustified(2, QLatin1Char('0'));
    }

    // Flushed at once so a daemon restart before the next connect still re-asserts it.
    inline void saveDesired(QSettings &settings, quint8 id, const QByteArray &payload)
    {
        settings.setValue(settingsKey(id), payload);
        settings.sync();
    }

    // Only a non-empty byte array can have come from saveDesired; a hand-edited "1a=07" reads back as the text "07" and must never reach the wire.
    inline std::optional<QByteArray> loadDesired(const QSettings &settings, quint8 id)
    {
        const QVariant value = settings.value(settingsKey(id));
        if (value.typeId() != QMetaType::QByteArray || value.toByteArray().isEmpty()) {
            return std::nullopt;
        }
        return value.toByteArray();
    }

    // Exactly what settingsKey writes after the group: two characters from 0-9 or a-f.
    inline bool isSavedIdKey(const QString &digits)
    {
        if (digits.size() != 2) {
            return false;
        }
        for (const QChar c : digits) {
            const bool decimal = c >= QLatin1Char('0') && c <= QLatin1Char('9');
            const bool lowerHex = c >= QLatin1Char('a') && c <= QLatin1Char('f');
            if (!decimal && !lowerHex) {
                return false;
            }
        }
        return true;
    }

    // Consumes keys shaped "PodSettings/1a"; the ini is case-sensitive here, so "1A" or "+1" is a key loadDesired could never find and is skipped.
    inline QList<quint8> desiredIds(const QSettings &settings)
    {
        const QString prefix = settingsGroup + QLatin1Char('/');
        QList<quint8> ids;
        for (const QString &key : settings.allKeys()) {
            if (!key.startsWith(prefix)) {
                continue;
            }
            const QString digits = key.mid(prefix.size());
            if (!isSavedIdKey(digits)) {
                continue;
            }
            ids.append(static_cast<quint8>(digits.toUInt(nullptr, 16)));
        }
        // allKeys() documents no order, so the ascending order callers rely on is made here rather than inherited.
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    // An empty request would go out as four zero bytes, which for 0x1A clears every mode from the cycle.
    inline std::optional<QByteArray> reassertFor(quint8 id, const QByteArray &desired,
                                                 const std::optional<QByteArray> &echoed)
    {
        if (!isSticky(id) || desired.isEmpty()) {
            return std::nullopt;
        }
        const QByteArray wanted = wireData(desired);
        if (echoed.has_value() && wireData(*echoed) == wanted) {
            return std::nullopt;
        }
        return ControlCommand::createCommand(id,
                                             static_cast<quint8>(wanted.at(0)),
                                             static_cast<quint8>(wanted.at(1)),
                                             static_cast<quint8>(wanted.at(2)),
                                             static_cast<quint8>(wanted.at(3)));
    }

    // "Re-asserting 0x1A: pods report 0f000000, want 07000000", or "pods report nothing" before any echo.
    inline QString reassertLogLine(quint8 id, const std::optional<QByteArray> &echoed,
                                   const QByteArray &desired)
    {
        const QString reported = echoed.has_value() ? QString::fromLatin1(wireData(*echoed).toHex())
                                                    : QStringLiteral("nothing");
        return QStringLiteral("Re-asserting 0x%1: pods report %2, want %3")
            .arg(QString::number(id, 16).rightJustified(2, QLatin1Char('0')).toUpper(),
                 reported, QString::fromLatin1(wireData(desired).toHex()));
    }
}
