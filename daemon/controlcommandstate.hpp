#pragma once

#include "BasicControlCommand.hpp"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QtGlobal>
#include <optional>

namespace OpenPods
{
// The floor ControlCommand::parseActive accepts: header, id and one value byte.
inline constexpr int controlCommandMinBytes = 8;

// Every control command the daemon builds is header, id and four data bytes.
inline constexpr int controlCommandFrameBytes = 11;

inline constexpr int controlCommandIdIndex = 6;
inline constexpr int controlCommandPayloadIndex = 7;

// idsSeen() is evidence that an id echoed, not a list of settings: 0x0D, 0x1B, 0x28, 0x2C, 0x2E and 0x06 land in it too.
class ControlCommandState
{
public:
    struct Recorded
    {
        bool accepted;
        std::optional<QString> warning;
    };

    // Sample input: 04 00 04 00 09 00 34 01 00 00 00 (allow-off enabled)
    Recorded record(const QByteArray &frame)
    {
        // L2CAP delivers whole frames, so a short one is not a split read and is dropped like a foreign header.
        if (!frame.startsWith(ControlCommand::HEADER) || frame.size() < controlCommandMinBytes) {
            return {false, std::nullopt};
        }
        const auto id = static_cast<quint8>(frame.at(controlCommandIdIndex));
        m_payloads.insert(id, frame.mid(controlCommandPayloadIndex));
        if (frame.size() != controlCommandFrameBytes) {
            return {true, QStringLiteral("Control command 0x%1 arrived with %2 bytes, expected %3")
                              .arg(QString::number(id, 16).rightJustified(2, QLatin1Char('0')).toUpper())
                              .arg(frame.size())
                              .arg(controlCommandFrameBytes)};
        }
        return {true, std::nullopt};
    }

    std::optional<QByteArray> payload(quint8 id) const
    {
        const auto it = m_payloads.constFind(id);
        if (it == m_payloads.cend()) {
            return std::nullopt;
        }
        return it.value();
    }

    // QMap iterates in key order, which is what makes this ascending.
    QList<quint8> idsSeen() const { return m_payloads.keys(); }

    void clear() { m_payloads.clear(); }

private:
    QMap<quint8, QByteArray> m_payloads;
};
}
