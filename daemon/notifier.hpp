#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// Split out of TrayIconManager so a headless run still gets user-visible toasts.
class Notifier : public QObject
{
    Q_OBJECT

public:
    // One replace id per channel, so a later toast on the same channel updates the last one instead of stacking.
    enum class Channel
    {
        Connected,
        Disconnected,
        LowBatteryLeft,
        LowBatteryRight,
        LowBatteryCase,
    };
    Q_ENUM(Channel)

    struct Options
    {
        QString urgency;
        int timeoutMs = -1;
        // A click runs this argv from the notification server, which lives outside the daemon's sandbox.
        QStringList execArgv;
    };

    static constexpr quint32 noReplaceId = 0;
    static constexpr const char *appName = "AirPods";
    static constexpr const char *glyph = "";

    explicit Notifier(QObject *parent = nullptr) : QObject(parent) {}

    bool enabled() const { return m_enabled; }

    void setEnabled(bool enabled)
    {
        if (m_enabled != enabled)
        {
            m_enabled = enabled;
            emit enabledChanged(enabled);
        }
    }

    // Sample output for a connect banner with no id yet: notification send --app-name AirPods -g <glyph> -u low -t 5000 -p "AirPods Pro connected" "Left 93%  Right 89%" --exec omarchy-shell omapods open
    // -p prints the id the server assigned, -r reuses it, and --exec must be last because it consumes the rest of the line.
    static QStringList arguments(const QString &title, const QString &message, const Options &options, quint32 replaceId)
    {
        QStringList args{QStringLiteral("notification"), QStringLiteral("send"),
                         QStringLiteral("--app-name"), QLatin1String(appName),
                         QStringLiteral("-g"), QString::fromUtf8(glyph)};
        if (!options.urgency.isEmpty())
        {
            args << QStringLiteral("-u") << options.urgency;
        }
        if (options.timeoutMs >= 0)
        {
            args << QStringLiteral("-t") << QString::number(options.timeoutMs);
        }
        if (replaceId != noReplaceId)
        {
            args << QStringLiteral("-r") << QString::number(replaceId);
        }
        args << QStringLiteral("-p") << title << message;
        if (!options.execArgv.isEmpty())
        {
            args << QStringLiteral("--exec") << options.execArgv;
        }
        return args;
    }

    // Sample: left 93 available, right 89 available, case absent -> "Left 93%  Right 89%"; nothing available -> "".
    // The case reports nothing while the pods are worn, so an absent component is skipped rather than shown as 0%.
    static QString batteryBanner(bool leftAvailable, int left, bool rightAvailable, int right,
                                 bool caseAvailable, int caseLevel)
    {
        QStringList parts;
        if (leftAvailable)
        {
            parts << QStringLiteral("Left %1%").arg(left);
        }
        if (rightAvailable)
        {
            parts << QStringLiteral("Right %1%").arg(right);
        }
        if (caseAvailable)
        {
            parts << QStringLiteral("Case %1%").arg(caseLevel);
        }
        return parts.join(QStringLiteral("  "));
    }

    quint32 replaceIdFor(Channel channel) const { return m_replaceIds.value(static_cast<int>(channel), noReplaceId); }

    // A default argument cannot name Options here (its member initializers are not complete yet), so the plain form is an overload.
    void notify(Channel channel, const QString &title, const QString &message)
    {
        notify(channel, title, message, Options());
    }

    // Without omarchy on PATH the tray takes over through fallbackRequested, and headless has no listener.
    void notify(Channel channel, const QString &title, const QString &message, const Options &options)
    {
        if (!m_enabled) {
            return;
        }
        const QString omarchy = QStandardPaths::findExecutable(QStringLiteral("omarchy"));
        if (omarchy.isEmpty()) {
            emit fallbackRequested(title, message);
            return;
        }
        auto *process = new QProcess(this);
        const int key = static_cast<int>(channel);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), process,
                [this, process, key](int exitCode, QProcess::ExitStatus) {
                    // omarchy-notification-send prints the id the server assigned when -p is given.
                    bool ok = false;
                    const quint32 id = process->readAllStandardOutput().trimmed().toUInt(&ok);
                    if (exitCode == 0 && ok && id != noReplaceId) {
                        m_replaceIds.insert(key, id);
                    }
                    process->deleteLater();
                });
        process->start(omarchy, arguments(title, message, options, replaceIdFor(channel)));
    }

signals:
    void enabledChanged(bool enabled);
    void fallbackRequested(const QString &title, const QString &message);

private:
    bool m_enabled = true;
    QHash<int, quint32> m_replaceIds;
};
