#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <optional>

#include "airpods_packets.h"

namespace OpenPods::Conversation
{
enum class Action
{
    Duck,
    Restore,
    Ignore
};

// Ignore is the default, so a Duck whose Restore never arrives needs this bound to give the volume back.
inline constexpr int caRestoreTimeoutMs = 20000;

inline constexpr int conversationFrameBytes = 10;
inline constexpr int levelIndex = 9;

// The levels are kept only for the timeout log line, so a long ducked run keeps this many and counts the rest.
inline constexpr int maxLevelsLoggedPerDuck = 32;

// 1 and 2 mean the wearer started speaking; 6 is normal volume again, 8 and 9 are the feature toggled off and on.
inline constexpr quint8 speakingLevelFirst = 1;
inline constexpr quint8 speakingLevelSecond = 2;
inline constexpr quint8 normalVolumeLevel = 6;
inline constexpr quint8 awarenessDisabledLevel = 8;
inline constexpr quint8 awarenessEnabledLevel = 9;

// Every level not listed is an intermediate ramp step and must leave the volume alone.
inline Action actionForLevel(quint8 level)
{
    switch (level) {
    case speakingLevelFirst:
    case speakingLevelSecond:
        return Action::Duck;
    case normalVolumeLevel:
    case awarenessDisabledLevel:
    case awarenessEnabledLevel:
        return Action::Restore;
    default:
        return Action::Ignore;
    }
}

// Sample: 04 00 04 00 4B 00 02 00 01 03 (opcode 0x4B, level 3 at index 9)
inline std::optional<quint8> levelFromFrame(const QByteArray &frame)
{
    if (frame.size() != conversationFrameBytes
        || !frame.startsWith(AirPodsPackets::ConversationalAwareness::DATA_HEADER)) {
        return std::nullopt;
    }
    return static_cast<quint8>(frame.at(levelIndex));
}

class LevelTracker
{
public:
    struct Decision
    {
        Action action;
        bool armTimeout;
        bool cancelTimeout;
        QString logLine;
    };

    Decision onLevel(quint8 level)
    {
        const Action wanted = actionForLevel(level);
        if (wanted == Action::Duck && !m_ducked) {
            m_ducked = true;
            m_levelsSinceDuck = QStringList{QString::number(level)};
            m_levelsNotLogged = 0;
            return {Action::Duck, true, false, QStringLiteral("CA level %1: duck").arg(level)};
        }
        if (wanted == Action::Restore && m_ducked) {
            forgetDuck();
            return {Action::Restore, false, true, QStringLiteral("CA level %1: restore").arg(level)};
        }
        if (!m_ducked) {
            return {Action::Ignore, false, false, QStringLiteral("CA level %1: ignore").arg(level)};
        }
        if (m_levelsSinceDuck.size() < maxLevelsLoggedPerDuck) {
            m_levelsSinceDuck.append(QString::number(level));
        } else {
            ++m_levelsNotLogged;
        }
        const QString reason = wanted == Action::Duck ? QStringLiteral("already ducked")
                                                      : QStringLiteral("still ducked");
        // A second Duck must not overwrite the saved volume with the already ducked one.
        return {Action::Ignore, true, false,
                QStringLiteral("CA level %1: ignore, %2").arg(level).arg(reason)};
    }

    Decision onTimeout()
    {
        if (!m_ducked) {
            return {Action::Ignore, false, false,
                    QStringLiteral("CA restore timeout fired while not ducked")};
        }
        QString levels = m_levelsSinceDuck.join(QLatin1Char(','));
        if (m_levelsNotLogged > 0) {
            levels += QStringLiteral(" and %1 more").arg(m_levelsNotLogged);
        }
        forgetDuck();
        return {Action::Restore, false, false,
                QStringLiteral("CA restore by timeout after levels %1").arg(levels)};
    }

    bool ducked() const { return m_ducked; }

private:
    void forgetDuck()
    {
        m_ducked = false;
        m_levelsSinceDuck.clear();
        m_levelsNotLogged = 0;
    }

    bool m_ducked = false;
    QStringList m_levelsSinceDuck;
    int m_levelsNotLogged = 0;
};
}
