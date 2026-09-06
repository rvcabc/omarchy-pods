#include <QSettings>
#include <QLocalServer>
#include <QSaveFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStyleHints>
#include <QPalette>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QQmlContext>
#include <csignal>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include <QBluetoothLocalDevice>
#include <QBluetoothSocket>
#include <QQuickWindow>
#include <QLoggingCategory>
#include <QThread>
#include <QTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QTranslator>
#include <QLibraryInfo>
#include <QDir>
#include <QStandardPaths>

#include "airpods_packets.h"
#include "logger.h"
#include "media/mediacontroller.h"
#include "trayiconmanager.h"
#include "notifier.hpp"
#include "enums.h"
#include <QRandomGenerator>
#include "battery.hpp"
#include "lowbatterywatcher.hpp"
#include "BluetoothMonitor.h"
#include "autostartmanager.hpp"
#include "deviceinfo.hpp"
#include "ipcpath.hpp"
#include "ipcverb.hpp"
#include "ble/blemanager.h"
#include "ble/bleutils.h"
#include "QRCodeImageProvider.hpp"
#include "systemsleepmonitor.hpp"
#include "controlreconnect.hpp"
#include "scanduty.hpp"
#include "controlcommandstate.hpp"
#include "metadata.hpp"
#include "modecycle.hpp"
#include "verbtable.hpp"
#include "settingsreassert.hpp"
#include "podsettings.hpp"
#include "audiosource.hpp"
#include "handoffstate.hpp"
#include "attclient.h"
#include <QElapsedTimer>

using namespace AirpodsTrayApp::Enums;

// Symbol and journald category string both `openpods` (renamed iter-51).
// Users filter via QT_LOGGING_RULES with `openpods.*`.
Q_LOGGING_CATEGORY(openpods, "openpods")

class AirPodsTrayApp : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool airpodsConnected READ areAirpodsConnected NOTIFY airPodsStatusChanged)
    Q_PROPERTY(int earDetectionBehavior READ earDetectionBehavior WRITE setEarDetectionBehavior NOTIFY earDetectionBehaviorChanged)
    Q_PROPERTY(bool crossDeviceEnabled READ crossDeviceEnabled WRITE setCrossDeviceEnabled NOTIFY crossDeviceEnabledChanged)
    Q_PROPERTY(AutoStartManager *autoStartManager READ autoStartManager CONSTANT)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY notificationsEnabledChanged)
    Q_PROPERTY(int retryAttempts READ retryAttempts WRITE setRetryAttempts NOTIFY retryAttemptsChanged)
    Q_PROPERTY(bool hideOnStart READ hideOnStart CONSTANT)
    Q_PROPERTY(DeviceInfo *deviceInfo READ deviceInfo CONSTANT)
    Q_PROPERTY(QString phoneMacStatus READ phoneMacStatus NOTIFY phoneMacStatusChanged)
    Q_PROPERTY(bool hearingAidEnabled READ hearingAidEnabled WRITE setHearingAidEnabled NOTIFY hearingAidEnabledChanged)

public:
    AirPodsTrayApp(bool debugMode, bool hideOnStart, bool headless, QQmlApplicationEngine *parent = nullptr)
        : QObject(parent), debugMode(debugMode), m_settings(new QSettings("AirPodsTrayApp", "AirPodsTrayApp"))
        , m_autoStartManager(new AutoStartManager(this)), m_hideOnStart(hideOnStart), parent(parent)
        , m_deviceInfo(new DeviceInfo(this)), m_bleManager(new BleManager(this))
        , m_systemSleepMonitor(new SystemSleepMonitor(this)), m_notifier(new Notifier(this))
    {
        QLoggingCategory::setFilterRules(QString("openpods.debug=%1").arg(debugMode ? "true" : "false"));
        LOG_INFO("Initializing OpenPods");
        restrictSettingsAccess();

        m_notifier->setEnabled(loadNotificationsEnabled());
        m_clock.start();
        connect(m_notifier, &Notifier::failed, this, [](const QString &detail) {
            LOG_WARN("Notification failed: " << detail);
        });
        m_att = new AttClient(this);
        connect(m_att, &AttClient::closed, this, [this](const QString &reason) {
            LOG_WARN("ATT channel closed: " << reason);
            emit airPodsStatusChanged();
        });
        connect(m_notifier, &Notifier::enabledChanged, this, &AirPodsTrayApp::saveNotificationsEnabled);
        connect(m_notifier, &Notifier::enabledChanged, this, &AirPodsTrayApp::notificationsEnabledChanged);
        connect(m_deviceInfo, &DeviceInfo::batteryStatusChanged, this, &AirPodsTrayApp::checkLowBatteryThresholds);

        // Headless has no tray to take the fallback, so say so rather than dropping the toast silently.
        if (headless) {
            connect(m_notifier, &Notifier::fallbackRequested, this, [](const QString &title, const QString &message) {
                LOG_WARN("omarchy is not on PATH, so this notification went nowhere: " << title << " " << message);
            });
        }

        // A headless run has no QSystemTrayIcon, which is what keeps Qt Widgets and Gui off this process.
        if (!headless) {
            trayManager = new TrayIconManager(this);
            connect(m_notifier, &Notifier::fallbackRequested, trayManager, &TrayIconManager::showTrayMessage);
            connect(trayManager, &TrayIconManager::trayClicked, this, &AirPodsTrayApp::onTrayIconActivated);
            connect(trayManager, &TrayIconManager::middleClicked, this, &AirPodsTrayApp::cycleNoiseControlMode);
            connect(trayManager, &TrayIconManager::openApp, this, &AirPodsTrayApp::onOpenApp);
            connect(trayManager, &TrayIconManager::openSettings, this, &AirPodsTrayApp::onOpenSettings);
            connect(trayManager, &TrayIconManager::noiseControlChanged, this, &AirPodsTrayApp::setNoiseControlMode);
            connect(trayManager, &TrayIconManager::conversationalAwarenessToggled, this, &AirPodsTrayApp::setConversationalAwareness);
            connect(m_deviceInfo, &DeviceInfo::batteryStatusChanged, trayManager, &TrayIconManager::updateBatteryStatus);
            connect(m_deviceInfo, &DeviceInfo::noiseControlModeChanged, trayManager, &TrayIconManager::updateNoiseControlState);
            connect(m_deviceInfo, &DeviceInfo::conversationalAwarenessChanged, trayManager, &TrayIconManager::updateConversationalAwareness);
        }

        // Initialize MediaController and connect signals
        mediaController = new MediaController(this);
        connect(mediaController, &MediaController::mediaStateChanged, this, &AirPodsTrayApp::handleMediaStateChange);
        mediaController->setFollowOnConnect(loadFollowOnConnect());
        mediaController->followMediaChanges();

        monitor = new BluetoothMonitor(this);
        refreshHearingGate();
        connect(monitor, &BluetoothMonitor::deviceConnected, this, &AirPodsTrayApp::bluezDeviceConnected);
        connect(monitor, &BluetoothMonitor::deviceDisconnected, this, &AirPodsTrayApp::bluezDeviceDisconnected);
        connect(monitor, &BluetoothMonitor::deviceConnectionProbeFinished,
                this, &AirPodsTrayApp::controlConnectionProbeFinished);

        // The control socket can drop while BlueZ keeps A2DP up, so Device1::Connected never rises again.
        m_controlReconnectTimer = new QTimer(this);
        m_controlReconnectTimer->setSingleShot(true);
        connect(m_controlReconnectTimer, &QTimer::timeout,
                this, &AirPodsTrayApp::attemptControlReconnect);

        // The bounded retries can exhaust while BlueZ still holds the device, and nothing else would fire again.
        m_controlWatchdogTimer = new QTimer(this);
        m_controlWatchdogTimer->setInterval(ControlReconnect::watchdogIntervalMs);
        connect(m_controlWatchdogTimer, &QTimer::timeout,
                this, &AirPodsTrayApp::checkControlLinkWatchdog);
        m_controlWatchdogTimer->start();

        connect(m_bleManager, &BleManager::deviceFound, this, &AirPodsTrayApp::bleDeviceFound);
        connect(m_deviceInfo->getBattery(), &Battery::primaryChanged, this, &AirPodsTrayApp::primaryChanged);
        connect(m_systemSleepMonitor, &SystemSleepMonitor::systemGoingToSleep, this, &AirPodsTrayApp::onSystemGoingToSleep);
        connect(m_systemSleepMonitor, &SystemSleepMonitor::systemWakingUp, this, &AirPodsTrayApp::onSystemWakingUp);

        // Load settings
        CrossDevice.isEnabled = loadCrossDeviceEnabled();
        setEarDetectionBehavior(loadEarDetectionSettings());
        setRetryAttempts(loadRetryAttempts());
        // Restore persisted PhoneMAC into the process env BEFORE
        // connectToPhone() reads it. setPhoneMac persists to QSettings
        // (iter-bugfix) so user doesn't have to re-enter on every
        // restart. If the saved value is invalid (manually edited
        // settings file), setPhoneMac surfaces the validation error.
        if (m_settings) {
            const QString savedMac = m_settings->value(QStringLiteral("phoneMac"), QString()).toString();
            if (!savedMac.isEmpty()) {
                setPhoneMac(savedMac);
            }
        }

        m_inConnectedSweep = true;

        monitor->checkAlreadyConnectedDevices();

        m_inConnectedSweep = false;
        LOG_INFO("AirPodsTrayApp initialized");

        QBluetoothLocalDevice localDevice;

        const QList<QBluetoothAddress> connectedDevices = localDevice.connectedDevices();
        for (const QBluetoothAddress &address : connectedDevices) {
            QBluetoothDeviceInfo device(address, "", 0);
            if (isAirPodsDevice(device)) {
                connectToDevice(device);

                // On startup after reboot, activate A2DP profile for already connected AirPods
                QTimer::singleShot(2000, this, [this, address]()
                {
                    // A disconnect inside these two seconds would otherwise start a chain for a device that left.
                    if (!areAirpodsConnected())
                    {
                        LOG_INFO("AirPods disconnected before the startup A2DP activation, skipping it");
                        return;
                    }

                    QString formattedAddress = address.toString().replace(":", "_");
                    LOG_INFO("A2DP profile activation attempted for AirPods found on startup");
                    mediaController->activateA2dpProfileWithRetry(formattedAddress);
                });
                return;
            }
        }

        initializeDBus();
        initializeBluetooth();
    }

    ~AirPodsTrayApp() override {
        saveCrossDeviceEnabled();
        saveEarDetectionSettings();

        // Reliability summary on shutdown so journald has a trendline
        // even without an exposed metrics endpoint.
        LOG_INFO("Reliability counters: reconnect_attempts_total="
                 << m_reconnectAttemptsTotal
                 << " reconnect_failures_total=" << m_reconnectFailuresTotal
                 << " noise_control_changes_total=" << m_noiseControlChangesTotal
                 << " forget_calls_total=" << m_forgetCallsTotal
                 << " ear_detection_changes_total=" << m_earDetectionChangesTotal
                 << " ca_changes_total=" << m_caChangesTotal
                 << " disconnect_calls_total=" << m_disconnectCallsTotal
                 << " connect_calls_total=" << m_connectCallsTotal
                 << " disconnect_failures_total=" << m_disconnectFailuresTotal
                 << " connect_failures_total=" << m_connectFailuresTotal
                 << " adaptive_level_changes_total=" << m_adaptiveLevelChangesTotal
                 << " one_bud_anc_changes_total=" << m_oneBudANCChangesTotal
                 << " reopen_calls_total=" << m_reopenCallsTotal);

        // Use deleteLater so any queued signal already in flight finishes
        // dispatching before the QObject vanishes. A bare `delete` here
        // races with the QBluetoothSocket's own emit chain.
        if (socket) {
            socket->disconnect(this);
            socket->deleteLater();
            socket = nullptr;
        }
        if (phoneSocket) {
            phoneSocket->disconnect(this);
            phoneSocket->deleteLater();
            phoneSocket = nullptr;
        }
    }

    bool areAirpodsConnected() const { return socket && socket->isOpen() && socket->state() == QBluetoothSocket::SocketState::ConnectedState; }
    int earDetectionBehavior() const { return mediaController->getEarDetectionBehavior(); }
    bool crossDeviceEnabled() const { return CrossDevice.isEnabled; }
    AutoStartManager *autoStartManager() const { return m_autoStartManager; }
    bool notificationsEnabled() const { return m_notifier->enabled(); }
    void setNotificationsEnabled(bool enabled) { m_notifier->setEnabled(enabled); }
    int retryAttempts() const { return m_retryAttempts; }
    bool hideOnStart() const { return m_hideOnStart; }
    DeviceInfo *deviceInfo() const { return m_deviceInfo; }
    QString phoneMacStatus() const { return m_phoneMacStatus; }
    bool hearingAidEnabled() const { return m_deviceInfo->hearingAidEnabled(); }

private:
    bool debugMode;
    bool isConnectedLocally = false;

    QQmlApplicationEngine *parent = nullptr;

    struct {
        bool isAvailable = true;
        bool isEnabled = true; // Ability to disable the feature
    } CrossDevice;

    void initializeDBus() { }

    bool isAirPodsDevice(const QBluetoothDeviceInfo &device)
    {
        return device.serviceUuids().contains(QBluetoothUuid("74ec2172-0bad-4d01-8f77-997b2be0722a"));
    }

    void notifyAndroidDevice()
    {
        if (!CrossDevice.isEnabled) {
            return;
        }

        if (phoneSocket && phoneSocket->isOpen())
        {
            phoneSocket->write(AirPodsPackets::Phone::NOTIFICATION);
            LOG_DEBUG("Sent notification packet to Android: " << AirPodsPackets::Phone::NOTIFICATION.toHex());
        }
        else
        {
            LOG_WARN("Phone socket is not open, cannot send notification packet");
        }
    }

    void disconnectDevice(const QString &devicePath) {
        LOG_INFO("Disconnecting device at " << devicePath);
    }

public slots:
    // Apple-style Disconnect / Connect controls. Wrap `bluetoothctl`
    // with the saved BD_ADDR. Async QProcess + deleteLater so the Qt
    // event loop doesn't block on the BlueZ IO round-trip. Refuses if
    // the daemon hasn't learned an address yet (initial pairing not
    // done). The connect path also triggers the existing
    // bluezDeviceConnected handler, which re-runs the AAP handshake.
    // The address a verb should act on: the live link's, else the paired device BlueZ lists with the AAP service, else the last one seen.
    QString getAirPodsAddress() {
        if (m_deviceInfo && !m_deviceInfo->bluetoothAddress().isEmpty()) {
            return m_deviceInfo->bluetoothAddress();
        }
        if (monitor) {
            const QString paired = monitor->findPairedAirPodsAddress();
            if (!paired.isEmpty()) {
                m_lastAirPodsAddress = paired;
                return paired;
            }
        }
        return m_lastAirPodsAddress;
    }

    // Setters answer with the refusal text a verb prints, or an empty string once the request went out.
    QString disconnectAirPods() {
        const QString addr = getAirPodsAddress();
        if (addr.isEmpty()) {
            LOG_WARN("disconnectAirPods: no current address to disconnect");
            return QStringLiteral("no AirPods address is known yet");
        }
        LOG_INFO("disconnectAirPods: " << addr);
        ++m_disconnectCallsTotal;
        m_disconnectRequested = true;
        auto *proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                proc, [this, proc, addr](int code, QProcess::ExitStatus) {
                    // INFO only on failure so journald isn't spammed
                    // with one success line per disconnect; the per-IPC
                    // disconnectAirPods entry above already records
                    // intent. Failures still surface for triage.
                    if (code != 0) {
                        LOG_INFO("bluetoothctl disconnect " << addr << " exit=" << code
                                 << " out=" << proc->readAllStandardOutput().trimmed());
                        ++m_disconnectFailuresTotal;
                    } else {
                        LOG_DEBUG("bluetoothctl disconnect " << addr << " ok");
                    }
                    proc->deleteLater();
                });
        proc->start("bluetoothctl", QStringList() << "disconnect" << addr);
        return {};
    }

    QString connectAirPods() {
        const QString addr = getAirPodsAddress();
        if (addr.isEmpty()) {
            LOG_WARN("connectAirPods: no current address to connect");
            return QStringLiteral("no AirPods address is known yet");
        }
        LOG_INFO("connectAirPods: " << addr);
        ++m_connectCallsTotal;
        auto *proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                proc, [this, proc, addr](int code, QProcess::ExitStatus) {
                    // Same pattern as disconnect lambda: INFO on
                    // failure only, DEBUG on the success path. Most
                    // connect calls succeed; spam reduction matters.
                    if (code != 0) {
                        LOG_INFO("bluetoothctl connect " << addr << " exit=" << code
                                 << " out=" << proc->readAllStandardOutput().trimmed());
                        ++m_connectFailuresTotal;
                    } else {
                        LOG_DEBUG("bluetoothctl connect " << addr << " ok");
                    }
                    proc->deleteLater();
                });
        proc->start("bluetoothctl", QStringList() << "connect" << addr);
        return {};
    }

    // Apple "Forget This Device" parity. Walks bluetoothctl with the
    // currently-connected BD_ADDR so the kernel BlueZ stack drops the
    // pairing keys + cached profile data. Caller is responsible for
    // confirming intent — there's no in-process undo.
    //
    // Async (own QProcess + deleteLater on finish) so we don't block
    // the Qt event loop on bluetoothctl's IO. The address comes from
    // DeviceInfo; refuses if no device is currently associated.
    QString forgetDevice() {
        const QString addr = getAirPodsAddress();
        if (addr.isEmpty()) {
            LOG_WARN("forgetDevice: no current device address to forget");
            return QStringLiteral("no AirPods address is known yet");
        }
        LOG_INFO("Forgetting device: " << addr);
        ++m_forgetCallsTotal;
        auto *proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                proc, [proc, addr](int code, QProcess::ExitStatus) {
                    if (code == 0) {
                        LOG_INFO("bluetoothctl remove " << addr << " ok: "
                                 << proc->readAllStandardOutput().trimmed());
                    } else {
                        LOG_ERROR("bluetoothctl remove " << addr << " exit " << code
                                  << ": " << proc->readAllStandardError().trimmed());
                    }
                    proc->deleteLater();
                });
        proc->start("bluetoothctl", QStringList() << "remove" << addr);
        return {};
    }

    void connectToDevice(const QString &address) {
        LOG_INFO("Connecting to device with address: " << address);
        QBluetoothAddress btAddress(address);
        QBluetoothDeviceInfo device(btAddress, "", 0);
        connectToDevice(device);
    }

    // The pods ignore a mode the model lacks without a reply, so the refusal has to come from here.
    QString refusalForMode(NoiseControlMode mode) const
    {
        const AirPodsModel model = m_deviceInfo->model();
        // An unknown model keeps every mode, the same fail-open choice supportsNoiseControl makes.
        if (model == AirPodsModel::Unknown) {
            return {};
        }
        const QString name = modelDisplayName(model);
        if (!supportsNoiseControl(model)) {
            return QStringLiteral("%1 has no listening modes").arg(name);
        }
        if (mode == NoiseControlMode::Off && !supportsNoiseOff(model)) {
            return QStringLiteral("%1 has no Off listening mode").arg(name);
        }
        if (mode == NoiseControlMode::Adaptive && !supportsAdaptiveAudio(model)) {
            return QStringLiteral("%1 has no Adaptive listening mode").arg(name);
        }
        return {};
    }

    QString setNoiseControlMode(NoiseControlMode mode)
    {
        if (!m_deviceInfo) {
            return QStringLiteral("device info is not ready");
        }
        if (const QString refusal = refusalForMode(mode); !refusal.isEmpty()) {
            LOG_WARN("Refusing listening mode " << static_cast<int>(mode) << ": " << refusal);
            return refusal;
        }
        if (m_deviceInfo->noiseControlMode() == mode)
        {
            LOG_DEBUG("Noise control mode is already set to: " << static_cast<int>(mode));
            return {};
        }
        LOG_INFO("Setting noise control mode to: " << mode);
        QByteArray packet = AirPodsPackets::NoiseControl::getPacketForMode(mode);
        if (!writePacketToSocket(packet, "Noise control mode packet written: ")) {
            return QStringLiteral("AirPods control link is not connected");
        }
        ++m_noiseControlChangesTotal;
        return {};
    }
    QString setNoiseControlModeInt(int mode)
    {
        if (mode < 0 || mode > static_cast<int>(NoiseControlMode::Adaptive))
        {
            LOG_ERROR("Invalid noise control mode: " << mode);
            return QStringLiteral("listening mode %1 is out of range").arg(mode);
        }
        return setNoiseControlMode(static_cast<NoiseControlMode>(mode));
    }

    // Walk every battery source on each battery-status change and
    // delegate the trip/reset decision to its LowBatteryLatch. Format
    // + emit the user-visible toast here; the latch owns no UI. The
    // global notificationsEnabled setting still gates emission via
    // Notifier::notify.
    void checkLowBatteryThresholds()
    {
        if (!m_deviceInfo) return;
        Battery *b = m_deviceInfo->getBattery();
        if (!b) return;

        struct Source {
            const char *label;
            quint8 level;
            bool available;
            bool charging;
            LowBatteryLatch *latch;
            Notifier::Channel channel;
        };
        const Source sources[] = {
            { "Left",  b->getLeftPodLevel(),  b->isLeftPodAvailable(),
              b->isLeftPodCharging(),  &m_lowBatteryLatchLeft, Notifier::Channel::LowBatteryLeft },
            { "Right", b->getRightPodLevel(), b->isRightPodAvailable(),
              b->isRightPodCharging(), &m_lowBatteryLatchRight, Notifier::Channel::LowBatteryRight },
            { "Case",  b->getCaseLevel(),     b->isCaseAvailable(),
              b->isCaseCharging(),     &m_lowBatteryLatchCase, Notifier::Channel::LowBatteryCase },
        };

        Notifier::Options options;
        options.urgency = QStringLiteral("normal");
        for (const Source &s : sources)
        {
            const auto fire = s.latch->evaluate(s.level, s.available, s.charging);
            if (fire) {
                m_notifier->notify(s.channel,
                    tr("%1 AirPod Low Battery").arg(QString::fromLatin1(s.label)),
                    tr("%1% remaining").arg(*fire), options);
            }
        }
    }

    // Cycle the noise-control mode in the same order the PodsMenu
    // segmented displays: Off -> NoiseCancellation -> Transparency ->
    // Adaptive -> Off. Bound to the tray icon middle-click and to the
    // "noise:cycle" IPC verb so other surfaces (keyboard shortcuts,
    // openpods-ctl) can reuse the rotation without duplicating the
    // state machine.
    QString cycleNoiseControlMode()
    {
        if (!m_deviceInfo)
        {
            LOG_ERROR("Cannot cycle noise control mode: device info not ready");
            return QStringLiteral("device info is not ready");
        }
        const AirPodsModel model = m_deviceInfo->model();
        const int current = static_cast<int>(m_deviceInfo->noiseControlMode());
        const int next = OpenPods::nextNoiseMode(current, supportsNoiseOff(model), supportsAdaptiveAudio(model));
        return setNoiseControlModeInt(next);
    }

    QString setConversationalAwareness(bool enabled)
    {
        if (!m_deviceInfo) {
            LOG_WARN("setConversationalAwareness: m_deviceInfo not ready");
            return QStringLiteral("device info is not ready");
        }
        const AirPodsModel model = m_deviceInfo->model();
        if (model != AirPodsModel::Unknown && !supportsConversationalAwareness(model)) {
            return QStringLiteral("%1 has no Conversation Awareness").arg(modelDisplayName(model));
        }
        if (m_deviceInfo->conversationalAwareness() == enabled) {
            LOG_DEBUG("Conversational awareness already " << (enabled ? "enabled" : "disabled"));
            return {};
        }
        LOG_INFO("Setting conversational awareness to: " << (enabled ? "enabled" : "disabled"));
        QByteArray packet = enabled ? AirPodsPackets::ConversationalAwareness::ENABLED
                                    : AirPodsPackets::ConversationalAwareness::DISABLED;

        if (!writePacketToSocket(packet, "Conversational awareness packet written: ")) {
            return QStringLiteral("AirPods control link is not connected");
        }
        m_deviceInfo->setConversationalAwareness(enabled);
        ++m_caChangesTotal;
        return {};
    }

    QString setOneBudANCMode(bool enabled)
    {
        const AirPodsModel model = m_deviceInfo->model();
        if (model != AirPodsModel::Unknown && !supportsOneBudANC(model)) {
            return QStringLiteral("%1 has no One-Bud ANC").arg(modelDisplayName(model));
        }
        if (m_deviceInfo->oneBudANCMode() == enabled)
        {
            LOG_DEBUG("One Bud ANC mode is already " << (enabled ? "enabled" : "disabled"));
            return {};
        }

        LOG_INFO("Setting One Bud ANC mode to: " << (enabled ? "enabled" : "disabled"));
        QByteArray packet = enabled ? AirPodsPackets::OneBudANCMode::ENABLED
                                    : AirPodsPackets::OneBudANCMode::DISABLED;

        if (!writePacketToSocket(packet, "One Bud ANC mode packet written: "))
        {
            LOG_ERROR("Failed to send One Bud ANC mode command: socket not open");
            return QStringLiteral("AirPods control link is not connected");
        }
        m_deviceInfo->setOneBudANCMode(enabled);
        ++m_oneBudANCChangesTotal;
        return {};
    }

    void setRetryAttempts(int attempts)
    {
        if (m_retryAttempts != attempts)
        {
            LOG_DEBUG("Setting retry attempts to: " << attempts);
            m_retryAttempts = attempts;
            emit retryAttemptsChanged(attempts);
            saveRetryAttempts(attempts);
        }
    }

    void initiateMagicPairing()
    {
        if (!socket || !socket->isOpen())
        {
            LOG_ERROR("Socket nicht offen, Magic Pairing kann nicht gestartet werden");
            return;
        }

        writePacketToSocket(AirPodsPackets::MagicPairing::REQUEST_MAGIC_CLOUD_KEYS, "Magic Pairing packet written: ");
    }

    QString setAdaptiveNoiseLevel(int level)
    {
        level = qBound(0, level, 100);
        // The pods accept the level only in Adaptive, so outside it the verb is refused rather than swallowed.
        if (!m_deviceInfo->adaptiveModeActive())
        {
            return QStringLiteral("adaptive level applies only while noise_mode is adaptive");
        }
        if (m_deviceInfo->adaptiveNoiseLevel() == level)
        {
            return {};
        }
        QByteArray packet = AirPodsPackets::AdaptiveNoise::getPacket(level);
        if (!writePacketToSocket(packet, "Adaptive noise level packet written: "))
        {
            return QStringLiteral("AirPods control link is not connected");
        }
        m_deviceInfo->setAdaptiveNoiseLevel(level);
        ++m_adaptiveLevelChangesTotal;
        return {};
    }

    QString renameAirPods(const QString &newName)
    {
        // The frame carries the length in one byte and the pods cap it at 32, counted in UTF-8 bytes, not characters.
        const qsizetype bytes = newName.toUtf8().size();
        const std::optional<QByteArray> packet = AirPodsPackets::Rename::getPacket(newName);
        if (!packet)
        {
            return QStringLiteral("name is %1 bytes, limit is %2").arg(bytes).arg(AirPodsPackets::Rename::renameMaxBytes);
        }
        if (newName == m_deviceInfo->deviceName())
        {
            LOG_DEBUG("Name is already set to: " << newName);
            return {};
        }
        if (!writePacketToSocket(*packet, "Rename packet written: "))
        {
            return QStringLiteral("AirPods control link is not connected");
        }
        LOG_INFO("Sent rename command for new name: " << newName);
        m_deviceInfo->setDeviceName(newName);
        return {};
    }

    // The bytes a setting holds right now: what the pods echoed this session, else what the user last asked for.
    std::optional<QByteArray> currentSettingBytes(quint8 id) const
    {
        if (const auto echoed = m_controlCommands.payload(id))
        {
            return OpenPods::Reassert::wireData(*echoed);
        }
        if (!m_settings)
        {
            return std::nullopt;
        }
        return OpenPods::Reassert::loadDesired(*m_settings, id);
    }

    // Every plain control-command setting goes through here; the table in podsettings.hpp is the spec.
    QString setPodSetting(const OpenPods::PodSettings::Spec &spec, const OpenPods::Ipc::Parsed &parsed)
    {
        if (!m_deviceInfo || !m_settings)
        {
            return QStringLiteral("device info is not ready");
        }
        const OpenPods::PodSettings::Request request{parsed.choice, parsed.number};
        const AirPodsModel model = m_deviceInfo->model();
        if (const QString refusal = OpenPods::PodSettings::refusal(spec, request, supportsNoiseOff(model),
                                                                   supportsAdaptiveAudio(model));
            !refusal.isEmpty())
        {
            return refusal;
        }
        const std::optional<QByteArray> current = currentSettingBytes(spec.id);
        const QByteArray data = spec.kind == OpenPods::PodSettings::Kind::Sides
                                    ? OpenPods::PodSettings::encodeSides(request, current)
                                    : OpenPods::PodSettings::encode(spec, request);
        const std::optional<QByteArray> echoed = m_controlCommands.payload(spec.id);
        if (echoed && OpenPods::Reassert::wireData(*echoed) == data)
        {
            LOG_DEBUG("Setting 0x" << QString::number(spec.id, 16) << " already holds " << data.toHex());
            OpenPods::Reassert::saveDesired(*m_settings, spec.id, data);
            restrictSettingsAccess();
            return {};
        }
        const QByteArray frame = ControlCommand::createCommand(spec.id, static_cast<quint8>(data.at(0)),
                                                               static_cast<quint8>(data.at(1)),
                                                               static_cast<quint8>(data.at(2)),
                                                               static_cast<quint8>(data.at(3)));
        if (!writePacketToSocket(frame, "Setting packet written: "))
        {
            return QStringLiteral("AirPods control link is not connected");
        }
        // Persisted only once the packet went out, so a refused verb never shows up in status.json as if it applied.
        OpenPods::Reassert::saveDesired(*m_settings, spec.id, data);
        restrictSettingsAccess();
        ++m_settingChangesTotal;
        // Turning the on-bud ear detection off also changes the notification mask the pods expect.
        if (spec.id == earDetectionConfigId)
        {
            writePacketToSocket(requestNotificationsPacket(), "Request notifications packet written: ");
        }
        return {};
    }

    // The mask follows the persisted on-bud ear detection wish, so a reconnect keeps the pods quiet about the ears.
    QByteArray requestNotificationsPacket() const
    {
        const std::optional<QByteArray> earDetection = currentSettingBytes(earDetectionConfigId);
        if (earDetection && !earDetection->isEmpty() && static_cast<quint8>(earDetection->at(0)) == controlCommandOff)
        {
            return AirPodsPackets::Connection::REQUEST_NOTIFICATIONS_EAR_DETECTION_OFF;
        }
        return AirPodsPackets::Connection::REQUEST_NOTIFICATIONS;
    }

    QString setCustomEq(const QString &text)
    {
        const std::optional<OpenPods::PodSettings::EqRequest> request = OpenPods::PodSettings::parseEq(text);
        if (!request)
        {
            return QStringLiteral("eq needs on|off:low:mid:high, each 0-100");
        }
        const std::optional<QByteArray> packet =
            AirPodsPackets::CustomEq::getPacket(request->enabled, request->low, request->mid, request->high);
        if (!packet)
        {
            return QStringLiteral("eq bands must be 0-100");
        }
        if (!writePacketToSocket(*packet, "Custom EQ packet written: "))
        {
            return QStringLiteral("AirPods control link is not connected");
        }
        if (m_settings)
        {
            m_settings->setValue(QStringLiteral("CustomEq/request"), text);
            restrictSettingsAccess();
        }
        ++m_settingChangesTotal;
        return {};
    }

    void setEarDetectionBehavior(int behavior)
    {
        if (behavior == earDetectionBehavior())
        {
            LOG_DEBUG("Ear detection behavior is already set to: " << behavior);
            return;
        }

        mediaController->setEarDetectionBehavior(static_cast<MediaController::EarDetectionBehavior>(behavior));
        saveEarDetectionSettings();
        ++m_earDetectionChangesTotal;
        emit earDetectionBehaviorChanged(behavior);
    }

    void setCrossDeviceEnabled(bool enabled)
    {
        if (CrossDevice.isEnabled == enabled)
        {
            LOG_DEBUG("Cross-device feature is already " << (enabled ? "enabled" : "disabled"));
            return;
        }

        CrossDevice.isEnabled = enabled;
        saveCrossDeviceEnabled();
        connectToPhone();
        emit crossDeviceEnabledChanged(enabled);
    }

    void setPhoneMac(const QString &mac)
    {
        if (mac.isEmpty()) {
            LOG_WARN("Empty MAC provided, ignoring");
            m_phoneMacStatus = QStringLiteral("No MAC provided (ignoring)");
            emit phoneMacStatusChanged();
            return;
        }

        // Basic MAC address validation (accepts formats like AA:BB:CC:DD:EE:FF, AABBCCDDEEFF, AA-BB-CC-DD-EE-FF)
        QRegularExpression re("^([0-9A-Fa-f]{2}([-:]?)){5}[0-9A-Fa-f]{2}$");
        if (!re.match(mac).hasMatch()) {
            LOG_ERROR("Invalid MAC address format: " << mac);
            m_phoneMacStatus = QStringLiteral("Invalid MAC: ") + mac;
            emit phoneMacStatusChanged();
            return;
        }

        // Persist via QSettings so restart picks it up; previously only
        // env var was set in-process and was lost on daemon exit. User
        // had to re-enter on every launch.
        if (m_settings) {
            m_settings->setValue(QStringLiteral("phoneMac"), mac);
            m_settings->sync();
        }

        // Set environment variable for the running process
        qputenv("PHONE_MAC_ADDRESS", mac.toUtf8());
        LOG_INFO("PHONE_MAC_ADDRESS environment variable set to: " << mac);

        m_phoneMacStatus = QStringLiteral("Updated MAC: ") + mac;
        emit phoneMacStatusChanged();

        // Update QML context property so UI placeholders reflect the new value
        if (parent) {
            parent->rootContext()->setContextProperty("PHONE_MAC_ADDRESS", mac);
        }

        // If a phone socket exists, restart connection using the new MAC
        if (phoneSocket && phoneSocket->isOpen()) {
            phoneSocket->close();
            phoneSocket->deleteLater();
            phoneSocket = nullptr;
        }
        connectToPhone();
    }

    void updatePhoneMacStatus(const QString &status)
    {
        m_phoneMacStatus = status;
        emit phoneMacStatusChanged();
    }

    // The pods ignore every hearing packet from a host whose DID is not Apple's, so the gate is checked here, not on the wire.
    QString hearingRefusal() const
    {
        if (!m_hearingGateReady) {
            return QStringLiteral("hearing features need DeviceID = bluetooth:004C:0000:0000 in /etc/bluetooth/main.conf (then sudo systemctl restart bluetooth)");
        }
        if (!areAirpodsConnected()) {
            return QStringLiteral("AirPods control link is not connected");
        }
        return {};
    }

    void refreshHearingGate()
    {
        const QString modalias = monitor ? monitor->adapterModalias() : QString();
        const bool ready = modalias.startsWith(QLatin1String("bluetooth:v004C"), Qt::CaseInsensitive);
        if (ready != m_hearingGateReady) {
            LOG_INFO("Hearing gate " << (ready ? "ready" : "closed") << ", adapter Modalias " << modalias);
        }
        m_hearingGateReady = ready;
    }

    QString setHearingAidEnabled(bool enabled)
    {
        if (const QString refusal = hearingRefusal(); !refusal.isEmpty()) {
            return refusal;
        }
        LOG_INFO("Setting hearing aid to: " << (enabled ? "enabled" : "disabled"));
        QByteArray packet = enabled ? AirPodsPackets::HearingAid::ENABLED
                                    : AirPodsPackets::HearingAid::DISABLED;

        if (!writePacketToSocket(packet, "Hearing aid packet written: ")) {
            return QStringLiteral("AirPods control link is not connected");
        }
        m_deviceInfo->setHearingAidEnabled(enabled);
        return {};
    }

    QString setHearingAssist(bool enabled)
    {
        if (const QString refusal = hearingRefusal(); !refusal.isEmpty()) {
            return refusal;
        }
        const QByteArray packet = enabled ? AirPodsPackets::HearingAssist::ENABLED : AirPodsPackets::HearingAssist::DISABLED;
        if (!writePacketToSocket(packet, "Hearing assist packet written: ")) {
            return QStringLiteral("AirPods control link is not connected");
        }
        OpenPods::Reassert::saveDesired(*m_settings, AirPodsPackets::HearingAssist::Type::ID,
                                        QByteArray(1, static_cast<char>(enabled ? 0x01 : 0x02)));
        restrictSettingsAccess();
        ++m_settingChangesTotal;
        return {};
    }

    // Opens the ATT channel on first use; the answer to the verb is immediate, the read-back lands in status.json.
    QString ensureAttOpen()
    {
        if (m_att->isOpen() || m_att->isConnecting()) {
            return {};
        }
        QString reason;
        if (!m_att->open(m_deviceInfo->bluetoothAddress(), &reason)) {
            return QStringLiteral("ATT channel could not open: %1").arg(reason);
        }
        // The transparency blob is read once per channel so the panel can show what the pods hold.
        m_att->read(OpenPods::Att::transparencyHandle, [this](bool ok, const QString &detail, const QByteArray &value) {
            if (ok) {
                m_transparencyCustom = value;
                emit airPodsStatusChanged();
            } else {
                LOG_WARN("Transparency read failed: " << detail);
            }
        });
        return {};
    }

    QString setLoudSoundReduction(bool enabled)
    {
        if (const QString refusal = hearingRefusal(); !refusal.isEmpty()) {
            return refusal;
        }
        if (const QString refusal = ensureAttOpen(); !refusal.isEmpty()) {
            return refusal;
        }
        const QByteArray value(1, static_cast<char>(enabled ? OpenPods::Att::loudSoundReductionOn : OpenPods::Att::loudSoundReductionOff));
        m_att->write(OpenPods::Att::loudSoundReductionHandle, value, [this](bool ok, const QString &detail, const QByteArray &) {
            if (!ok) {
                LOG_ERROR("Loud Sound Reduction write refused: " << detail);
                return;
            }
            // The CCCD on this handle does not work, so the state is read back rather than notified.
            m_att->read(OpenPods::Att::loudSoundReductionHandle, [this](bool readOk, const QString &readDetail, const QByteArray &readValue) {
                if (!readOk || readValue.isEmpty()) {
                    LOG_ERROR("Loud Sound Reduction read failed: " << readDetail);
                    return;
                }
                m_loudSoundReduction = static_cast<quint8>(readValue.at(0)) == OpenPods::Att::loudSoundReductionOn;
                LOG_INFO("Loud Sound Reduction is " << (*m_loudSoundReduction ? "on" : "off"));
                emit airPodsStatusChanged();
            });
        });
        ++m_settingChangesTotal;
        return {};
    }

    bool writePacketToSocket(const QByteArray &packet, const QString &logMessage)
    {
        if (socket && socket->isOpen())
        {
            socket->write(packet);
            LOG_DEBUG(logMessage << packet.toHex());
            return true;
        }
        else
        {
            LOG_ERROR("Socket is not open, cannot write packet");
            return false;
        }
    }

    // The settings file holds the pairing keys, so keep it and its directory owner only.
    void restrictSettingsAccess()
    {
        if (!m_settings) {
            return;
        }
        m_settings->sync();
        const QString file = m_settings->fileName();
        QFile::setPermissions(QFileInfo(file).absolutePath(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        if (QFile::exists(file)) {
            QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }

    bool loadCrossDeviceEnabled() { return m_settings->value("crossdevice/enabled", false).toBool(); }
    void saveCrossDeviceEnabled() { m_settings->setValue("crossdevice/enabled", CrossDevice.isEnabled); }

    int loadEarDetectionSettings() { return m_settings->value("earDetection/setting", MediaController::EarDetectionBehavior::PauseWhenOneRemoved).toInt(); }
    void saveEarDetectionSettings() { m_settings->setValue("earDetection/setting", mediaController->getEarDetectionBehavior()); }

    bool loadNotificationsEnabled() const { return m_settings->value("notifications/enabled", true).toBool(); }
    void saveNotificationsEnabled(bool enabled) { m_settings->setValue("notifications/enabled", enabled); }

    int loadRetryAttempts() const { return m_settings->value("bluetooth/retryAttempts", 3).toInt(); }
    void saveRetryAttempts(int attempts) { m_settings->setValue("bluetooth/retryAttempts", attempts); }

    // Discovery stays off for the whole time the control link is up, whatever controller this box has.
    void stopBleScanWhileConnected()
    {
        if (!areAirpodsConnected() || !m_bleManager->isScanning())
            return;

        LOG_INFO("Stopping BLE scan while AirPods control link is connected");
        m_bleManager->stopScan();
    }

    void onSystemGoingToSleep()
    {
        m_isSuspending = true;
        if (m_bleManager->isScanning())
        {
            LOG_INFO("Stopping BLE scan before going to sleep");
            m_bleManager->stopScan();
        }
    }
    void onSystemWakingUp()
    {
        LOG_INFO("System wake-up; deferring BLE rediscovery " << ScanDuty::resumeSettleMs
                 << "ms for BlueZ and any reconnecting HID device to settle");

        // BlueZ + the kernel BT controller often need ~1-3s after resume
        // before hci0 is responsive again. Firing scan + GetManagedObjects
        // immediately on the PrepareForSleep:false signal races the stack
        // and the first scan typically returns no devices. Wait a beat,
        // and keep m_isSuspending true until the grace window closes so
        // the disconnect notifications that BlueZ fires during resume
        // don't surface as user-visible "AirPods Disconnected" toasts.
        // The wait also covers bonded BLE HID devices, which re-associate in
        // the first seconds after the controller resets and lose the race
        // against a scan that starts while they are still advertising.
        QTimer::singleShot(ScanDuty::resumeSettleMs, this, [this]() {
            // Suspend stopped discovery on every controller, so resume restarts it and the gate below re-applies.
            m_bleManager->startScan();

            if (areAirpodsConnected() && m_deviceInfo && !m_deviceInfo->bluetoothAddress().isEmpty())
            {
                stopBleScanWhileConnected();
                LOG_INFO("AirPods already connected after wake-up, re-activating A2DP profile");
                // Profile may have been dropped during suspend; reassert.
                QTimer::singleShot(1000, this, [this]() {
                    LOG_INFO("A2DP profile activation attempted after system wake-up");
                    mediaController->activateA2dpProfileWithRetry(m_deviceInfo->bluetoothAddress().replace(":", "_"));
                });
            }

            m_inConnectedSweep = true;

            monitor->checkAlreadyConnectedDevices();

            m_inConnectedSweep = false;
            m_isSuspending = false;
        });
    }

private slots:
    void onTrayIconActivated()
    {
        // --hide defers the QML load, so a tray click before the first reopen finds no window at all.
        const auto windows = QGuiApplication::topLevelWindows();
        if (windows.isEmpty()) {
            loadMainModule();
            return;
        }
        QQuickWindow *window = qobject_cast<QQuickWindow *>(windows.constFirst());
        if (window)
        {
            window->show();
            window->raise();
            window->requestActivate();
        }
    }

    void onOpenApp()
    {
        if (!parent) return;
        // Guard against empty rootObjects: QList::first() on an empty
        // list is undefined behavior, not a null return. After the
        // lazy-QML deferral in main() (--hide skips eager
        // loadMainModule), rootObjects can be empty until the first
        // reopen — which is exactly when this handler fires.
        const auto roots = parent->rootObjects();
        if (!roots.isEmpty()) {
            QMetaObject::invokeMethod(roots.first(), "reopen", Q_ARG(QVariant, "app"));
        } else {
            loadMainModule();
        }
    }

    void onOpenSettings()
    {
        if (!parent) return;
        const auto roots = parent->rootObjects();
        if (!roots.isEmpty()) {
            QMetaObject::invokeMethod(roots.first(), "reopen", Q_ARG(QVariant, "settings"));
        } else {
            // First-time settings click with no QML loaded yet: just
            // load Main.qml. It opens to the main page by default;
            // user clicks the settings icon to drill in. One extra
            // step on first-launch vs. allocating ~60-100MB of QML
            // overhead eagerly at every daemon start.
            loadMainModule();
        }
    }

    void sendHandshake() {
        LOG_INFO("Connected to device, sending initial packets");
        writePacketToSocket(AirPodsPackets::Connection::HANDSHAKE, "Handshake packet written: ");
    }

    void bluezDeviceConnected(const QString &address, const QString &name)
    {
        // Only a connect BlueZ announced earns the banner; a sweep finding the pods already connected is a daemon start.
        m_bannerEligible = !m_inConnectedSweep;
        rememberAirPodsDevice(address, name);
        m_disconnectFinalized = false;

        QBluetoothDeviceInfo device(QBluetoothAddress(address), name, 0);
        const bool recovering = m_controlRecovery.isActive();
        if (recovering) {
            if (m_controlRecovery.state() == ControlReconnect::State::ConnectingSocket && socket) {
                LOG_DEBUG("Control recovery connection is already in progress");
                return;
            }
            m_controlReconnectTimer->stop();
            m_controlRecovery.beginConnection();
        }
        connectToDevice(device, recovering);

        // After system reboot, AirPods might be connected but A2DP profile not active
        // Attempt to activate A2DP profile after a delay to ensure connection is established
        if (!recovering) QTimer::singleShot(2000, this, [this, address]()
        {
            if (!address.isEmpty())
            {
                QString formattedAddress = address;
                formattedAddress = formattedAddress.replace(":", "_");
                LOG_INFO("A2DP profile activation attempted for newly connected device");
                mediaController->activateA2dpProfileWithRetry(formattedAddress);
            }
        });
    }

    void onDeviceDisconnected(const QBluetoothAddress &address)
    {
        LOG_INFO("Device disconnected: " << address.toString());
        // A retry still in flight would reactivate a profile for the device that just went away.
        mediaController->cancelPendingA2dpActivation();
        if (socket)
        {
            LOG_WARN("Socket is still open, closing it");
            socket->disconnect(this);
            socket->close();
            socket->deleteLater();
            socket = nullptr;
        }

        // BlueZ lowers Connected while rebuilding a profile, so try the control socket before tearing down.
        scheduleControlReconnect(address.toString(), m_lastAirPodsName,
                                 QStringLiteral("BlueZ disconnect event"));
    }

    // Apple devices overwrite the sticky settings on every connect, so the persisted wish is sent again after the echo burst.
    void reassertStickySettings()
    {
        if (!areAirpodsConnected() || !m_settings) {
            return;
        }
        for (const quint8 id : OpenPods::Reassert::desiredIds(*m_settings)) {
            if (!OpenPods::Reassert::isSticky(id)) {
                continue;
            }
            const auto desired = OpenPods::Reassert::loadDesired(*m_settings, id);
            if (!desired) {
                continue;
            }
            const auto echoed = m_controlCommands.payload(id);
            if (const auto frame = OpenPods::Reassert::reassertFor(id, *desired, echoed)) {
                LOG_INFO(OpenPods::Reassert::reassertLogLine(id, echoed, *desired));
                writePacketToSocket(*frame, "Re-asserted setting packet written: ");
            }
        }
    }

    void finalizeDeviceDisconnected(const QString &address)
    {
        if (m_disconnectFinalized) {
            return;
        }
        m_disconnectFinalized = true;
        m_controlReconnectTimer->stop();
        m_controlRecovery.cancel();
        m_retryCount = 0;
        m_controlCommands.clear();

        if (phoneSocket && phoneSocket->isOpen())
        {
            phoneSocket->write(AirPodsPackets::Connection::AIRPODS_DISCONNECTED);
            LOG_DEBUG("AIRPODS_DISCONNECTED packet written: " << AirPodsPackets::Connection::AIRPODS_DISCONNECTED.toHex());
        }

        // The pods are gone, so playback that was on them stops rather than jumping to the speakers.
        if (mediaController->getCurrentMediaState() == MediaController::MediaState::Playing)
        {
            LOG_INFO("AirPods disconnected while playing, pausing playback here");
            mediaController->pause();
        }
        // Clear the device name and model
        m_deviceInfo->reset();
        m_bleManager->startScan();
        emit airPodsStatusChanged();

        // Skip the disconnect toast if we're suspending/resuming — BlueZ
        // fires PropertiesChanged Connected=false on suspend and the user
        // doesn't want to see "AirPods Disconnected" every time they close
        // the lid. Tray icon still resets so visual state is accurate.
        // A disconnect the user asked for through the verb needs no toast either.
        if (!m_isSuspending && !m_disconnectRequested) {
            m_notifier->notify(Notifier::Channel::Disconnected,
                tr("AirPods Disconnected"),
                tr("Your AirPods have been disconnected"));
        }
        m_disconnectRequested = false;
        m_connectedBannerPending = false;
        mediaController->startFollowSession();
        m_att->close();
        m_loudSoundReduction.reset();
        m_transparencyCustom.clear();
        if (trayManager) {
            trayManager->resetTrayIcon();
        }
    }

    void rememberAirPodsDevice(const QString &address, const QString &name)
    {
        if (!address.isEmpty()) {
            m_lastAirPodsAddress = address;
        }
        if (!name.isEmpty()) {
            m_lastAirPodsName = name;
        }
    }

    void scheduleControlReconnect(const QString &address, const QString &name,
                                  const QString &reason)
    {
        rememberAirPodsDevice(address, name);
        if (m_lastAirPodsAddress.isEmpty() || m_isSuspending) {
            finalizeDeviceDisconnected(address);
            return;
        }

        const bool bleScanWasActive = m_bleManager->isScanning();

        // A live scan delays the L2CAP connect this recovery depends on, so pause it and restore after.
        m_bleManager->stopScan();

        if (!m_controlRecovery.isActive()) {
            m_controlRecovery.begin(bleScanWasActive);
            m_disconnectFinalized = false;
            LOG_INFO("Scheduling AirPods control reconnect after " << reason);
            m_controlReconnectTimer->start(ControlReconnect::firstDelayMs);
        } else if (m_controlRecovery.state() == ControlReconnect::State::ConnectingSocket) {
            scheduleControlRecoveryRetry(reason, false);
        }
    }

    // Gated on the last probe answer, so pods that are merely away cannot restart the ladder every tick.
    void checkControlLinkWatchdog()
    {
        if (ControlReconnect::shouldRescanFromWatchdog(
                areAirpodsConnected(), m_controlRecovery.isActive(), m_isSuspending,
                !m_lastAirPodsAddress.isEmpty())) {
            if (monitor->checkAlreadyConnectedDevices()) {
                LOG_INFO("Control link watchdog: swept up AirPods that no BlueZ signal announced");
            }
            return;
        }

        if (!ControlReconnect::shouldRetryFromWatchdog(
                areAirpodsConnected(), m_controlRecovery.isActive(), m_isSuspending,
                !m_lastAirPodsAddress.isEmpty(), m_bluezReportedConnected)) {
            return;
        }

        LOG_INFO("Control link watchdog: last BlueZ probe said connected, retrying "
                 << m_lastAirPodsAddress);
        scheduleControlReconnect(m_lastAirPodsAddress, m_lastAirPodsName,
                                 QStringLiteral("watchdog recheck"));
    }

    void attemptControlReconnect()
    {
        if (m_controlRecovery.state() != ControlReconnect::State::Waiting) {
            return;
        }

        if (areAirpodsConnected()) {
            finishControlRecovery();
            return;
        }

        const QString address = m_lastAirPodsAddress;
        if (address.isEmpty()) {
            finalizeDeviceDisconnected(address);
            return;
        }

        const quint64 requestId = m_controlRecovery.beginProbe();
        monitor->probeDeviceConnected(address, requestId);
    }

    void controlConnectionProbeFinished(const QString &address, quint64 requestId,
                                        bool connected)
    {
        if (address.compare(m_lastAirPodsAddress, Qt::CaseInsensitive) != 0
            || !m_controlRecovery.acceptsProbe(requestId)) {
            LOG_DEBUG("Ignoring stale AirPods connection probe");
            return;
        }

        m_bluezReportedConnected = connected;
        if (connected) {
            ++m_reconnectAttemptsTotal;
            LOG_INFO("Reconnecting AirPods control link (attempt total="
                     << m_reconnectAttemptsTotal << ")");
            m_controlRecovery.beginConnection();
            QBluetoothDeviceInfo device(QBluetoothAddress(address), m_lastAirPodsName, 0);
            connectToDevice(device, true);
            return;
        }

        scheduleControlRecoveryRetry(QStringLiteral("BlueZ did not report the device as connected"), false);
    }

    // deviceStillConnected picks the longer ladder, because BlueZ saying it has the device means the endpoint is there.
    void scheduleControlRecoveryRetry(const QString &reason, bool deviceStillConnected)
    {
        if (m_controlRecovery.prepareRetry(m_retryAttempts, deviceStillConnected)) {
            const int jitter = static_cast<int>(QRandomGenerator::global()->bounded(ControlReconnect::jitterRangeMs));
            const int spent = deviceStillConnected ? m_controlRecovery.connectedAttempts()
                                                   : m_controlRecovery.absentProbes();
            const int limit = deviceStillConnected ? ControlReconnect::connectedAttemptLimit
                                                   : m_retryAttempts;
            // Each branch backs off on its own count, so the one number in the line explains the delay beside it.
            const int delay = ControlReconnect::delayMs(spent, jitter);
            LOG_INFO("Retrying AirPods control recovery after " << reason << " ("
                     << spent << "/" << limit << ", delay=" << delay << "ms)");
            m_controlReconnectTimer->start(delay);
            return;
        }

        ++m_reconnectFailuresTotal;
        if (deviceStillConnected) {
            LOG_WARN("AirPods never accepted the control socket while BlueZ held "
                     << m_lastAirPodsAddress << ", finalizing");
        } else {
            LOG_INFO("AirPods stayed disconnected through control recovery, finalizing "
                     << m_lastAirPodsAddress);
        }
        finalizeDeviceDisconnected(m_lastAirPodsAddress);
    }

    void finishControlRecovery()
    {
        if (!m_controlRecovery.isActive()) {
            return;
        }

        m_controlReconnectTimer->stop();
        const bool restoreBleScan = m_controlRecovery.complete();
        m_disconnectFinalized = false;
        if (restoreBleScan) {
            m_bleManager->startScan();
        } else {
            m_bleManager->stopScan();
        }
        LOG_INFO("AirPods control link recovered");

        // onDeviceDisconnected cancelled any in-flight activation, and a recovering connect skips the usual retry.
        if (!m_lastAirPodsAddress.isEmpty()) {
            QString formattedAddress = m_lastAirPodsAddress;
            formattedAddress.replace(":", "_");
            mediaController->activateA2dpProfileWithRetry(formattedAddress);
        }
    }

    void bluezDeviceDisconnected(const QString &address, const QString &name)
    {
        if (address == m_deviceInfo->bluetoothAddress())
        {
            onDeviceDisconnected(QBluetoothAddress(address));
        } else {
            LOG_WARN("Disconnected device does not match connected device: " << address << " != " << m_deviceInfo->bluetoothAddress());
        }
    }

    void parseMetadata(const QByteArray &data)
    {
        const OpenPods::MetadataParse parsed = OpenPods::parseMetadata(data);
        if (!parsed.ok)
        {
            LOG_ERROR("Invalid metadata packet: " << parsed.error);
            return;
        }
        m_deviceInfo->setDeviceName(parsed.value.name);
        m_deviceInfo->setModelNumber(parsed.value.modelNumber);
        m_deviceInfo->setManufacturer(parsed.value.manufacturer);
        m_deviceInfo->setSerialNumber(parsed.value.serialNumber);
        m_deviceInfo->setFirmwareVersion(parsed.value.firmwareVersion);
        m_deviceInfo->setHardwareRevision(parsed.value.hardwareRevision);
        m_deviceInfo->setLeftSerial(parsed.value.leftSerial);
        m_deviceInfo->setRightSerial(parsed.value.rightSerial);

        m_deviceInfo->setModel(parseModelNumber(m_deviceInfo->modelNumber()));
        emit modelChanged();

        // Persist the parsed metadata so loadFromSettings can re-run
        // parseModelNumber on next startup (lets newly-added enum map
        // entries like AirPodsPro3 A3064 resolve without waiting for
        // AAP metadata to re-fire).
        if (m_settings) {
            m_deviceInfo->saveToSettings(*m_settings);
            restrictSettingsAccess();
        }

        // Log extracted metadata
        LOG_INFO("Parsed AirPods metadata:");
        LOG_INFO("Device Name: " << m_deviceInfo->deviceName());
        LOG_INFO("Model Number: " << m_deviceInfo->modelNumber());
        LOG_INFO("Firmware: " << m_deviceInfo->firmwareVersion() << " hardware " << m_deviceInfo->hardwareRevision());
        LOG_INFO("Manufacturer: " << m_deviceInfo->manufacturer());
    }

    QString getEarStatus(char value)
    {
        return (value == 0x00) ? "In Ear" : (value == 0x01) ? "Out of Ear"
                                                            : "In case";
    }

    void connectToDevice(const QBluetoothDeviceInfo &device, bool controlRecovery = false)
    {
        if (socket && socket->isOpen() && socket->peerAddress() == device.address())
        {
            LOG_INFO("Already connected to the device: " << device.name());
            return;
        }

        LOG_INFO("Connecting to device: " << device.name());
        rememberAirPodsDevice(device.address().toString(), device.name());

        // Clean up any existing socket (defensive: disconnect first so any
        // queued slot can't reach a half-dead object).
        if (socket) {
            socket->disconnect(this);
            socket->close();
            socket->deleteLater();
            socket = nullptr;
        }

        QBluetoothSocket *localSocket = new QBluetoothSocket(QBluetoothServiceInfo::L2capProtocol, this);
        localSocket->setProperty("openpodsControlRecovery", controlRecovery);
        socket = localSocket;

        // Connection handler
        auto handleConnection = [this, localSocket]()
        {
            localSocket->setProperty("openpodsControlConnected", true);
            m_retryCount = 0;
            m_disconnectFinalized = false;
            if (localSocket->property("openpodsControlRecovery").toBool()) {
                finishControlRecovery();
            }
            stopBleScanWhileConnected();
            connect(localSocket, &QBluetoothSocket::readyRead, this, [this, localSocket]()
                    {
            QByteArray data = localSocket->readAll();
            QMetaObject::invokeMethod(this, "parseData", Qt::QueuedConnection, Q_ARG(QByteArray, data));
            QMetaObject::invokeMethod(this, "relayPacketToPhone", Qt::QueuedConnection, Q_ARG(QByteArray, data)); });
            sendHandshake();
        };

        // Error handler with retry. Per-device member instead of static so
        // attempts don't leak across devices (e.g. switching between two
        // paired AirPods).
        auto handleError = [this, device, localSocket](QBluetoothSocket::SocketError error)
        {
            LOG_ERROR("Socket error: " << error << ", " << localSocket->errorString());

            // Once a control link has been established, a later socket error
            // is a link-loss event, not another initial-connect failure.
            if (localSocket->property("openpodsControlConnected").toBool()) {
                handleControlSocketLoss(device, localSocket,
                                        QStringLiteral("socket error"));
                return;
            }

            if (localSocket->property("openpodsControlRecovery").toBool()) {
                handleControlConnectFailure(device, localSocket,
                                            QStringLiteral("socket error before connection"));
                return;
            }

            if (m_retryCount < m_retryAttempts)
            {
                m_retryCount++;
                m_reconnectAttemptsTotal++;
                // Exponential backoff with jitter: 1000 * 2^(n-1) + rand(0..499)ms.
                // n=1 -> ~1.0-1.5s, n=2 -> ~2.0-2.5s, n=3 -> ~4.0-4.5s.
                // Fixed-delay 1500ms hammered BlueZ if multiple peers were
                // also retrying after a controller reset — exponential
                // spacing + jitter avoids the thundering-herd reconnect.
                const int jitter = static_cast<int>(QRandomGenerator::global()->bounded(ControlReconnect::jitterRangeMs));
                const int delay = ControlReconnect::delayMs(m_retryCount, jitter);
                LOG_INFO("Retrying connection (attempt " << m_retryCount << "/" << m_retryAttempts
                         << ", delay=" << delay << "ms, total=" << m_reconnectAttemptsTotal << ")");
                QTimer::singleShot(delay, this, [this, device]()
                                   { connectToDevice(device); });
            }
            else
            {
                m_reconnectFailuresTotal++;
                LOG_ERROR("Failed to connect after " << m_retryAttempts
                          << " attempts (total failures=" << m_reconnectFailuresTotal << ")");
                m_retryCount = 0;
                // BlueZ never raises Connected again for a device it already holds, so hand over to the probe ladder.
                scheduleControlReconnect(device.address().toString(), device.name(),
                                         QStringLiteral("initial connect attempts exhausted"));
            }
        };

        connect(localSocket, &QBluetoothSocket::connected, this, handleConnection);
        connect(localSocket, QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::errorOccurred),
                this, handleError);
        connect(localSocket, &QBluetoothSocket::disconnected, this,
                [this, device, localSocket]() {
                    if (!localSocket->property("openpodsControlConnected").toBool()) {
                        if (localSocket->property("openpodsControlRecovery").toBool()) {
                            handleControlConnectFailure(
                                device, localSocket,
                                QStringLiteral("socket disconnected before connection"));
                        }
                        return;
                    }
                    handleControlSocketLoss(device, localSocket,
                                            QStringLiteral("socket disconnected"));
                });

        localSocket->connectToService(device.address(), QBluetoothUuid("74ec2172-0bad-4d01-8f77-997b2be0722a"));
        m_deviceInfo->setBluetoothAddress(device.address().toString());
        notifyAndroidDevice();
    }

    void handleControlConnectFailure(const QBluetoothDeviceInfo &device,
                                     QBluetoothSocket *failedSocket,
                                     const QString &reason)
    {
        if (!failedSocket || socket != failedSocket || !m_controlRecovery.isActive()) {
            return;
        }

        LOG_WARN("AirPods control reconnect failed: " << reason);
        failedSocket->disconnect(this);
        failedSocket->close();
        failedSocket->deleteLater();
        socket = nullptr;
        rememberAirPodsDevice(device.address().toString(), device.name());
        scheduleControlRecoveryRetry(reason, true);
    }

    void handleControlSocketLoss(const QBluetoothDeviceInfo &device,
                                 QBluetoothSocket *lostSocket,
                                 const QString &reason)
    {
        // errorOccurred and disconnected commonly arrive for the same loss;
        // only the first callback for the currently-owned socket may recover.
        if (!lostSocket || socket != lostSocket) {
            return;
        }

        LOG_WARN("AirPods control link lost: " << reason);
        lostSocket->disconnect(this);
        lostSocket->close();
        lostSocket->deleteLater();
        socket = nullptr;

        scheduleControlReconnect(device.address().toString(), device.name(), reason);
    }

    void parseData(const QByteArray &data)
    {
        LOG_DEBUG("Received: " << data.toHex());

        // Every control command is recorded before the chain below, so a later feature can prove its echo.
        const OpenPods::ControlCommandState::Recorded recorded = m_controlCommands.record(data);
        if (recorded.warning)
        {
            LOG_WARN(*recorded.warning);
        }

        if (data.startsWith(AirPodsPackets::Parse::HANDSHAKE_ACK))
        {
            writePacketToSocket(AirPodsPackets::Connection::SET_SPECIFIC_FEATURES, "Set specific features packet written: ");
        }
        else if (data.startsWith(AirPodsPackets::Parse::FEATURES_ACK))
        {
            // A control-link recovery on a link BlueZ never dropped is not a connect either, so eligibility is spent here.
            m_connectedBannerPending = m_bannerEligible;
            m_bannerEligible = false;
            // The audio-source frame names hosts by byte-reversed MAC, so this box's own is cached per link.
            m_localReversedMac = OpenPods::AudioSource::reversedMac(QBluetoothLocalDevice().address().toString());
            refreshHearingGate();
            writePacketToSocket(requestNotificationsPacket(), "Request notifications packet written: ");
            // Pods coming out of the case while something plays here should land on this box, so claim them now.
            if (mediaController->getCurrentMediaState() == MediaController::MediaState::Playing)
            {
                m_handoff.onLocalMedia(true, OpenPods::Handoff::Origin::User);
                if (writePacketToSocket(AirPodsPackets::OwnsConnection::CLAIM, "Handoff CLAIM packet written on connect: "))
                {
                    m_handoff.noteClaimSent(m_clock.elapsed());
                }
            }

            QTimer::singleShot(2000, this, [this]() {
                if (m_deviceInfo->batteryStatus().isEmpty()) {
                    writePacketToSocket(requestNotificationsPacket(), "Request notifications packet written: ");
                }
            });
            QTimer::singleShot(OpenPods::Reassert::reassertAfterNotificationsMs, this, [this]() {
                reassertStickySettings();
            });
        }
        // Magic Cloud Keys Response
        else if (data.startsWith(AirPodsPackets::MagicPairing::MAGIC_CLOUD_KEYS_HEADER))
        {
            auto keys = AirPodsPackets::MagicPairing::parseMagicCloudKeysPacket(data);
            // Sizes, never the key material: the journal is not where a pairing key belongs.
            LOG_INFO("Received Magic Cloud Keys: IRK " << keys.magicAccIRK.size()
                     << " bytes, EncKey " << keys.magicAccEncKey.size() << " bytes");

            // Store the keys
            m_deviceInfo->setMagicAccIRK(keys.magicAccIRK);
            m_deviceInfo->setMagicAccEncKey(keys.magicAccEncKey);
            m_deviceInfo->saveToSettings(*m_settings);
            restrictSettingsAccess();
        }
        // Get CA state
        else if (data.startsWith(AirPodsPackets::ConversationalAwareness::HEADER)) {
            if (auto result = AirPodsPackets::ConversationalAwareness::parseState(data))
            {
                m_deviceInfo->setConversationalAwareness(result.value());
                LOG_DEBUG("Conversational awareness state received: " << m_deviceInfo->conversationalAwareness());
            }
        }
        // Hearing Aid state
        else if (data.startsWith(AirPodsPackets::HearingAid::HEADER)) {
            if (auto result = AirPodsPackets::HearingAid::parseState(data))
            {
                m_deviceInfo->setHearingAidEnabled(result.value());
                LOG_DEBUG("Hearing aid state received: " << m_deviceInfo->hearingAidEnabled());
            }
        }
        // Noise Control Mode
        else if (data.size() == 11 && data.startsWith(AirPodsPackets::NoiseControl::HEADER))
        {
            if (auto value = AirPodsPackets::NoiseControl::parseMode(data))
            {
                m_deviceInfo->setNoiseControlMode(value.value());
                LOG_DEBUG("Noise control mode received: " << m_deviceInfo->noiseControlMode());
            }
        }
        // Ear Detection
        else if (data.size() == 8 && data.startsWith(AirPodsPackets::Parse::EAR_DETECTION))
        {
            m_deviceInfo->getEarDetection()->parseData(data);
            mediaController->handleEarDetection(m_deviceInfo->getEarDetection());
        }
        // Battery Status
        else if ((data.size() == 22 || data.size() == 12) && data.startsWith(AirPodsPackets::Parse::BATTERY_STATUS))
        {
            m_deviceInfo->getBattery()->parsePacket(data);
            m_deviceInfo->updateBatteryStatus();
            LOG_INFO("Battery status: " << m_deviceInfo->batteryStatus());
            if (m_connectedBannerPending)
            {
                m_connectedBannerPending = false;
                showBatteryBanner(tr("%1 connected").arg(podsDisplayName()),
                                  {QStringLiteral("omarchy-shell"), QStringLiteral("omapods"), QStringLiteral("open")});
            }
        }
        // Conversational Awareness Data
        else if (data.size() == 10 && data.startsWith(AirPodsPackets::ConversationalAwareness::DATA_HEADER))
        {
            LOG_INFO("Received conversational awareness data");
            mediaController->handleConversationalAwareness(data);
        }
        else if (data.startsWith(AirPodsPackets::Parse::METADATA))
        {
            parseMetadata(data);
            initiateMagicPairing();
            if (m_deviceInfo->getEarDetection()->oneOrMorePodsInEar()) // AirPods get added as output device only after this
            {
                mediaController->activateA2dpProfileWithRetry(m_deviceInfo->bluetoothAddress().replace(":", "_"));
            }
            else
            {
                mediaController->setConnectedDeviceMacAddress(m_deviceInfo->bluetoothAddress().replace(":", "_"));
            }
            stopBleScanWhileConnected();
            emit airPodsStatusChanged();
        }
        else if (data.startsWith(AirPodsPackets::OneBudANCMode::HEADER)) {
            if (auto value = AirPodsPackets::OneBudANCMode::parseState(data))
            {
                m_deviceInfo->setOneBudANCMode(value.value());
                LOG_DEBUG("One Bud ANC mode received: " << m_deviceInfo->oneBudANCMode());
            }
        }
        else if (data.startsWith(OpenPods::AudioSource::HEADER))
        {
            handleAudioSource(data);
        }
        else if (recorded.accepted)
        {
            // A control command with no branch of its own is exactly what the recorder is for.
            LOG_DEBUG("Control command recorded: " << data.toHex());
        }
        else
        {
            LOG_DEBUG("Unrecognized packet format: " << data.toHex());
        }
    }

    void connectToPhone() {
        if (!CrossDevice.isEnabled) {
            return;
        }

        if (phoneSocket && phoneSocket->isOpen()) {
            LOG_INFO("Already connected to the phone");
            return;
        }
        // If a previous attempt left a non-open socket lying around (e.g.
        // mid-connect or error state), drop it before allocating a new one
        // — otherwise we leak a QBluetoothSocket per call.
        if (phoneSocket) {
            phoneSocket->disconnect(this);
            phoneSocket->abort();
            phoneSocket->deleteLater();
            phoneSocket = nullptr;
        }

        QBluetoothAddress phoneAddress("00:00:00:00:00:00"); // Default address, will be overwritten if PHONE_MAC_ADDRESS is set
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

        if (!env.value("PHONE_MAC_ADDRESS").isEmpty())
        {
            phoneAddress = QBluetoothAddress(env.value("PHONE_MAC_ADDRESS"));
        }
        phoneSocket = new QBluetoothSocket(QBluetoothServiceInfo::L2capProtocol, this);
        connect(phoneSocket, &QBluetoothSocket::connected, this, [this]() {
            LOG_INFO("Connected to phone");
            if (!lastBatteryStatus.isEmpty()) {
                phoneSocket->write(lastBatteryStatus);
                LOG_DEBUG("Sent last battery status to phone: " << lastBatteryStatus.toHex());
            }
            if (!lastEarDetectionStatus.isEmpty()) {
                phoneSocket->write(lastEarDetectionStatus);
                LOG_DEBUG("Sent last ear detection status to phone: " << lastEarDetectionStatus.toHex());
            }
        });

        connect(phoneSocket, QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::errorOccurred), this, [this](QBluetoothSocket::SocketError error) {
            LOG_ERROR("Phone socket error: " << error << ", " << phoneSocket->errorString());
        });

        phoneSocket->connectToService(phoneAddress, QBluetoothUuid("1abbb9a4-10e4-4000-a75c-8953c5471342"));
    }

    void relayPacketToPhone(const QByteArray &packet)
    {
        if (!CrossDevice.isEnabled) {
            return;
        }
        if (phoneSocket && phoneSocket->isOpen())
        {
            phoneSocket->write(AirPodsPackets::Phone::NOTIFICATION + packet);
        }
        else
        {
            connectToPhone();
            LOG_WARN("Phone socket is not open, cannot relay packet");
        }
    }

    void handlePhonePacket(const QByteArray &packet) {
        if (packet.startsWith(AirPodsPackets::Phone::NOTIFICATION))
        {
            QByteArray airpodsPacket = packet.mid(4);
            if (socket && socket->isOpen()) {
                socket->write(airpodsPacket);
                LOG_DEBUG("Relayed packet to AirPods: " << airpodsPacket.toHex());
            } else {
                LOG_ERROR("Socket is not open, cannot relay packet to AirPods");
            }
        }
        else if (packet.startsWith(AirPodsPackets::Phone::CONNECTED))
        {
            LOG_INFO("AirPods connected");
            isConnectedLocally = true;
            CrossDevice.isAvailable = false;
        }
        else if (packet.startsWith(AirPodsPackets::Phone::DISCONNECTED))
        {
            LOG_INFO("AirPods disconnected");
            isConnectedLocally = false;
            CrossDevice.isAvailable = true;
        }
        else if (packet.startsWith(AirPodsPackets::Phone::STATUS_REQUEST))
        {
            LOG_INFO("Connection status request received");
            QByteArray response = (socket && socket->isOpen()) ? AirPodsPackets::Phone::CONNECTED
                                                               : AirPodsPackets::Phone::DISCONNECTED;
            phoneSocket->write(response);
            LOG_DEBUG("Sent connection status response: " << response.toHex());
        }
        else if (packet.startsWith(AirPodsPackets::Phone::DISCONNECT_REQUEST))
        {
            LOG_INFO("Disconnect request received");
            if (socket && socket->isOpen()) {
                socket->close();
                LOG_INFO("Disconnected from AirPods");
                // Async: don't block UI thread on bluetoothctl IO.
                auto *proc = new QProcess(this);
                connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                        proc, [proc](int, QProcess::ExitStatus) {
                            LOG_INFO("Bluetoothctl disconnect output: " << proc->readAllStandardOutput().trimmed());
                            proc->deleteLater();
                        });
                proc->start("bluetoothctl", QStringList() << "disconnect" << m_deviceInfo->bluetoothAddress());
                isConnectedLocally = false;
                CrossDevice.isAvailable = true;
            }
        }
        else
        {
            if (socket && socket->isOpen()) {
                socket->write(packet);
                LOG_DEBUG("Relayed packet to AirPods: " << packet.toHex());
            } else {
                LOG_ERROR("Socket is not open, cannot relay packet to AirPods");
            }
        }
    }

    void onPhoneDataReceived() {
        QByteArray data = phoneSocket->readAll();
        LOG_DEBUG("Data received from phone: " << data.toHex());
        QMetaObject::invokeMethod(this, "handlePhonePacket", Qt::QueuedConnection, Q_ARG(QByteArray, data));
    }

    void bleDeviceFound(const BleInfo &device)
    {
        if (BLEUtils::isValidIrkRpa(m_deviceInfo->magicAccIRK(), device.address)) {
            LOG_DEBUG("BLE adv accepted caseBattery=" << device.caseBattery << " charge=" << device.caseCharging << " primaryLeft=" << device.primaryLeft);
            // Only adopt the BLE-broadcast model when it's actually
            // recognized. AirPods Pro 3 (and any future model whose
            // manufacturer-data fingerprint hasn't been added to
            // ble/blemanager.cpp's modelMap yet) reports Unknown here,
            // which would otherwise clobber a valid model from the
            // AAP metadata packet on the L2CAP side.
            if (device.modelName != AirPodsModel::Unknown) {
                m_deviceInfo->setModel(device.modelName);
            }
            auto decryptet = BLEUtils::decryptLastBytes(device.encryptedPayload, m_deviceInfo->magicAccEncKey());
            m_deviceInfo->getBattery()->parseEncryptedPacket(decryptet, device.primaryLeft, device.isThisPodInTheCase, isModelHeadset(m_deviceInfo->model()));
            // Case battery isn't covered by parseEncryptedPacket when
            // pods are out of the case (podInCase=false), so feed the
            // BLE-broadcast case nibble in directly. device.caseBattery
            // is -1 when the broadcast nibble was 15 (unknown); the
            // setter skips those so we don't clobber a valid prior
            // reading.
            // A Max has no case, and its nibble decodes to 0 rather than the 15 that means unknown.
            if (!isModelHeadset(m_deviceInfo->model())) {
                m_deviceInfo->getBattery()->setCaseFromBle(device.caseBattery, device.caseCharging);
            }
            m_deviceInfo->getEarDetection()->overrideEarDetectionStatus(device.isPrimaryInEar, device.isSecondaryInEar);
            // macOS shows the battery card when the lid opens nearby; the banner is only useful while the link is down.
            const bool lidJustOpened = m_lidState != BleInfo::LidState::OPEN && device.lidState == BleInfo::LidState::OPEN;
            m_lidState = device.lidState;
            if (lidJustOpened && !areAirpodsConnected())
            {
                showBatteryBanner(podsDisplayName(), {});
            }
        }
    }

    QString podsDisplayName() const
    {
        if (m_deviceInfo && m_deviceInfo->model() != AirPodsModel::Unknown) {
            return modelDisplayName(m_deviceInfo->model());
        }
        if (m_deviceInfo && !m_deviceInfo->deviceName().isEmpty()) {
            return m_deviceInfo->deviceName();
        }
        return tr("AirPods");
    }

    // One channel for both the lid-open card and the connect banner, so the second replaces the first instead of stacking.
    void showBatteryBanner(const QString &title, const QStringList &execArgv)
    {
        if (!loadConnectedBannerEnabled() || !m_deviceInfo) {
            return;
        }
        Battery *b = m_deviceInfo->getBattery();
        const QString body = Notifier::batteryBanner(b->isLeftPodAvailable(), b->getLeftPodLevel(),
                                                     b->isRightPodAvailable(), b->getRightPodLevel(),
                                                     b->isCaseAvailable(), b->getCaseLevel());
        if (body.isEmpty()) {
            return;
        }
        Notifier::Options options;
        options.urgency = QStringLiteral("low");
        options.timeoutMs = connectedBannerTimeoutMs;
        options.execArgv = execArgv;
        LOG_INFO("Battery banner: " << title << ", " << body);
        m_notifier->notify(Notifier::Channel::Connected, title, body, options);
    }

public:
    bool loadConnectedBannerEnabled() const { return m_settings->value("notifications/connected", true).toBool(); }
    void saveConnectedBannerEnabled(bool enabled) { m_settings->setValue("notifications/connected", enabled); }
    bool loadFollowOnConnect() const { return m_settings->value("audio/followOnConnect", true).toBool(); }
    void saveFollowOnConnect(bool follow) { m_settings->setValue("audio/followOnConnect", follow); mediaController->setFollowOnConnect(follow); }

    void handleAudioSource(const QByteArray &data)
    {
        const std::optional<OpenPods::AudioSource::Info> info = OpenPods::AudioSource::parse(data);
        if (!info)
        {
            LOG_WARN("Audio source frame too short: " << data.toHex());
            return;
        }
        const bool otherDevice = info->deviceMac != m_localReversedMac;
        m_lastAudioSource = *info;
        m_lastAudioSourceOther = otherDevice;
        const OpenPods::Handoff::SourceDecision decision = m_handoff.onAudioSource(otherDevice, info->type, m_clock.elapsed());
        LOG_INFO(decision.logLine << " (device " << OpenPods::AudioSource::macTail(info->deviceMac) << ")");
        switch (decision.action)
        {
        case OpenPods::Handoff::Action::Interrupt:
            mediaController->rememberDefaultSinkForInterruption();
            mediaController->pause();
            break;
        case OpenPods::Handoff::Action::Resume:
            // The interruption stands until the reclaim went out, so a closed link tries again on the next frame.
            if (!areAirpodsConnected())
            {
                LOG_WARN("Cannot reclaim the pods: control link is not connected");
                break;
            }
            if (writePacketToSocket(AirPodsPackets::OwnsConnection::CLAIM, "Handoff CLAIM packet written on release: "))
            {
                m_handoff.noteClaimSent(m_clock.elapsed());
                m_handoff.noteResumed();
                mediaController->reclaimDefaultSinkAfterInterruption();
                mediaController->play();
            }
            break;
        case OpenPods::Handoff::Action::Ignore:
            break;
        }
        emit airPodsStatusChanged();
    }

public:
    void handleMediaStateChange(MediaController::MediaState state, MediaController::MediaOrigin origin) {
        const bool playing = state == MediaController::MediaState::Playing;
        const OpenPods::Handoff::Origin who = origin == MediaController::Daemon ? OpenPods::Handoff::Origin::Daemon
                                                                                : OpenPods::Handoff::Origin::User;
        const OpenPods::Handoff::MediaDecision decision = m_handoff.onLocalMedia(playing, who);
        LOG_DEBUG(decision.logLine);
        if (decision.wire == OpenPods::Handoff::Wire::Claim)
        {
            if (areAirpodsConnected())
            {
                if (writePacketToSocket(AirPodsPackets::OwnsConnection::CLAIM, "Handoff CLAIM packet written: "))
                {
                    m_handoff.noteClaimSent(m_clock.elapsed());
                }
                if (!mediaController->isActiveOutputDeviceAirPods())
                {
                    mediaController->activateA2dpProfileWithRetry(m_deviceInfo->bluetoothAddress().replace(":", "_"));
                }
            }
            else if (loadConnectOnPlay())
            {
                // Off by default: pulling the pods off the phone on every local play is what fights Apple's own switching.
                LOG_INFO("Media started here with the pods away, connecting them (handoff:connectonplay is on)");
                connectAirPods();
            }
            // The Android side channel is a separate feature with its own default-off gate.
            if (CrossDevice.isEnabled) {
                sendDisconnectRequestToAndroid();
                connectToAirPods(true);
            }
        }
        else if (decision.wire == OpenPods::Handoff::Wire::Release && areAirpodsConnected())
        {
            writePacketToSocket(AirPodsPackets::OwnsConnection::RELEASE, "Handoff RELEASE packet written: ");
        }
        emit airPodsStatusChanged();
    }

    bool loadConnectOnPlay() const { return m_settings->value("handoff/connectOnPlay", false).toBool(); }
    void saveConnectOnPlay(bool on) { m_settings->setValue("handoff/connectOnPlay", on); }

    void sendDisconnectRequestToAndroid()
    {
        if (!CrossDevice.isEnabled) return;

        if (phoneSocket && phoneSocket->isOpen())
        {
            phoneSocket->write(AirPodsPackets::Phone::DISCONNECT_REQUEST);
            LOG_DEBUG("Sent disconnect request to Android: " << AirPodsPackets::Phone::DISCONNECT_REQUEST.toHex());
        }
        else
        {
            LOG_WARN("Phone socket is not open, cannot send disconnect request");
        }
    }

    bool isPhoneConnected() {
        return phoneSocket && phoneSocket->isOpen();
    }

    void connectToAirPods(bool force) {
        if (socket && socket->isOpen()) {
            LOG_INFO("Already connected to AirPods");
            return;
        }

        if (force) {
            LOG_INFO("Forcing connection to AirPods");
            // Async: don't block UI on bluetoothctl. Once it finishes, walk
            // the connected-device list and dispatch to connectToDevice().
            auto *proc = new QProcess(this);
            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    proc, [this, proc](int, QProcess::ExitStatus) {
                        LOG_INFO("Bluetoothctl connect output: " << proc->readAllStandardOutput().trimmed());
                        proc->deleteLater();
                        QBluetoothLocalDevice localDevice;
                        const QList<QBluetoothAddress> connectedDevices = localDevice.connectedDevices();
                        for (const QBluetoothAddress &address : connectedDevices) {
                            QBluetoothDeviceInfo device(address, "", 0);
                            if (isAirPodsDevice(device)) {
                                connectToDevice(device);
                                return;
                            }
                        }
                    });
            proc->start("bluetoothctl", QStringList() << "connect" << m_deviceInfo->bluetoothAddress());
            return;
        }
        QBluetoothLocalDevice localDevice;
        const QList<QBluetoothAddress> connectedDevices = localDevice.connectedDevices();
        for (const QBluetoothAddress &address : connectedDevices) {
            QBluetoothDeviceInfo device(address, "", 0);
            LOG_DEBUG("Connected device: " << device.name() << " (" << device.address().toString() << ")");
            if (isAirPodsDevice(device)) {
                connectToDevice(device);
                return;
            }
        }
        LOG_WARN("AirPods not found among connected devices");
    }

    void initializeBluetooth() {
        connectToPhone();

        m_deviceInfo->loadFromSettings(*m_settings);
        // Unreachable with the pods already connected: the constructor returns before this.
        m_bleManager->startScan();
    }

    // Null engine is the headless run, where there is no window to open and nothing to say about it.
    void loadMainModule() {
        if (!parent) {
            LOG_INFO("Running headless, so there is no window to open");
            return;
        }
        parent->load(QUrl(QStringLiteral("qrc:/linux/Main.qml")));
    }

signals:
    void noiseControlModeChanged(NoiseControlMode mode);
    void earDetectionStatusChanged(const QString &status);
    void batteryStatusChanged(const QString &status);
    void conversationalAwarenessChanged(bool enabled);
    void adaptiveNoiseLevelChanged(int level);
    void deviceNameChanged(const QString &name);
    void modelChanged();
    void primaryChanged();
    void airPodsStatusChanged();
    void earDetectionBehaviorChanged(int behavior);
    void crossDeviceEnabledChanged(bool enabled);
    void notificationsEnabledChanged(bool enabled);
    void retryAttemptsChanged(int attempts);
    void oneBudANCModeChanged(bool enabled);
    void phoneMacStatusChanged();
    void hearingAidEnabledChanged(bool enabled);

private:
    QBluetoothSocket *socket = nullptr;
    QBluetoothSocket *phoneSocket = nullptr;
    QByteArray lastBatteryStatus;
    QByteArray lastEarDetectionStatus;
    MediaController* mediaController;
    // Null in a headless run; every use needs a guard.
    TrayIconManager *trayManager = nullptr;
    BluetoothMonitor *monitor;
    QTimer *m_controlReconnectTimer = nullptr;
    // Safety net behind m_controlReconnectTimer's bounded retries; see checkControlLinkWatchdog().
    QTimer *m_controlWatchdogTimer = nullptr;
    // Last answer BlueZ gave about this device, which is what separates a dead link from absent pods.
    bool m_bluezReportedConnected = false;
    QSettings *m_settings;
    AutoStartManager *m_autoStartManager;
    int m_retryAttempts = 3;
    int m_retryCount = 0;
    ControlReconnect::Session m_controlRecovery;
    bool m_isSuspending = false;
    bool m_disconnectFinalized = false;
    QString m_lastAirPodsAddress;
    QString m_lastAirPodsName;

    // Reliability counters — process-lifetime totals, logged on quit.
    // Exposed to QML readers via the public getters below.
    int m_reconnectAttemptsTotal = 0;
    int m_reconnectFailuresTotal = 0;
    // User-action counters. Increment AFTER the no-op short-circuit
    // (so noise:cycle on an already-correct mode doesn't inflate the
    // count) — gives a faithful signal of how often the user is
    // actually toggling. Useful for spotting wedged-mode bugs where
    // setNoiseControlMode is called repeatedly with the same value.
    int m_noiseControlChangesTotal = 0;
    int m_forgetCallsTotal = 0;
    int m_earDetectionChangesTotal = 0;
    int m_caChangesTotal = 0;
    int m_disconnectCallsTotal = 0;
    int m_connectCallsTotal = 0;
    int m_disconnectFailuresTotal = 0;
    int m_connectFailuresTotal = 0;
    int m_adaptiveLevelChangesTotal = 0;
    int m_oneBudANCChangesTotal = 0;
    int m_reopenCallsTotal = 0;

    // Every control command the pods echoed this session, keyed by id.
    OpenPods::ControlCommandState m_controlCommands;
    int m_settingChangesTotal = 0;
    // Set by the disconnect verb so the toast that follows is not shown for a disconnect the user asked for.
    bool m_disconnectRequested = false;
    // Armed at FEATURES_ACK and spent on the first battery frame, which is when the banner has numbers to show.
    bool m_connectedBannerPending = false;
    bool m_bannerEligible = false;
    bool m_inConnectedSweep = false;
    static constexpr int connectedBannerTimeoutMs = 5000;
    // Handoff with the other devices that share the pods, driven by the audio-source frames and local playback edges.
    OpenPods::Handoff::State m_handoff;
    QElapsedTimer m_clock;
    QByteArray m_localReversedMac;
    std::optional<OpenPods::AudioSource::Info> m_lastAudioSource;
    bool m_lastAudioSourceOther = false;
    // Hearing lives behind the Apple DeviceID gate and, for Loud Sound Reduction, on the ATT channel.
    AttClient *m_att = nullptr;
    bool m_hearingGateReady = false;
    std::optional<bool> m_loudSoundReduction;
    QByteArray m_transparencyCustom;
    // CC 0x0A toggles ear detection on the buds themselves; the host-side pause policy is separate.
    static constexpr quint8 earDetectionConfigId = 0x0A;
    static constexpr quint8 hearingAidControlId = 0x2C;
    static constexpr quint8 controlCommandOff = 0x02;

    // Low-battery notification latches per battery source. State +
    // hysteresis live in LowBatteryLatch (see lowbatterywatcher.hpp);
    // tst_lowbatterywatcher.cpp exhaustively covers the trip/reset
    // transitions so this slot only needs to feed the three sources
    // through their respective latches and format the message.
    LowBatteryLatch m_lowBatteryLatchLeft;
    LowBatteryLatch m_lowBatteryLatchRight;
    LowBatteryLatch m_lowBatteryLatchCase;

public:
    int reconnectAttemptsTotal() const { return m_reconnectAttemptsTotal; }
    int reconnectFailuresTotal() const { return m_reconnectFailuresTotal; }
    int noiseControlChangesTotal() const { return m_noiseControlChangesTotal; }
    int forgetCallsTotal() const { return m_forgetCallsTotal; }
    int earDetectionChangesTotal() const { return m_earDetectionChangesTotal; }
    int caChangesTotal() const { return m_caChangesTotal; }
    int disconnectCallsTotal() const { return m_disconnectCallsTotal; }
    int connectCallsTotal() const { return m_connectCallsTotal; }
    int disconnectFailuresTotal() const { return m_disconnectFailuresTotal; }
    int connectFailuresTotal() const { return m_connectFailuresTotal; }
    int adaptiveLevelChangesTotal() const { return m_adaptiveLevelChangesTotal; }
    int oneBudANCChangesTotal() const { return m_oneBudANCChangesTotal; }
    int reopenCallsTotal() const { return m_reopenCallsTotal; }
    void incReopenCallsTotal() { ++m_reopenCallsTotal; }

    // The status snapshot, shared by the IPC verb and the state file so the two
    // can never drift.
    QJsonObject statusJson()
    {
        DeviceInfo *d = deviceInfo();
        Battery *b = d ? d->getBattery() : nullptr;
        QJsonObject status;
        status.insert("schema_version", 1);
        status.insert("connected", areAirpodsConnected());
        status.insert("device_name", d ? d->deviceName() : QString());
        status.insert("noise_mode", d ? d->noiseControlModeInt() : -1);
        if (b) {
            auto pod = [&](bool avail, int level, bool charging, bool optimized, bool inEar) {
                QJsonObject o;
                o.insert("available", avail);
                o.insert("level", level);
                o.insert("charging", charging);
                o.insert("optimized_charging", optimized);
                o.insert("in_ear", inEar);
                return o;
            };
            status.insert("left",  pod(b->isLeftPodAvailable(),  b->getLeftPodLevel(),
                                       b->isLeftPodCharging(),   b->isLeftPodOptimizedCharging(),  d->isLeftPodInEar()));
            status.insert("right", pod(b->isRightPodAvailable(), b->getRightPodLevel(),
                                       b->isRightPodCharging(),  b->isRightPodOptimizedCharging(), d->isRightPodInEar()));
            QJsonObject caseObj;
            caseObj.insert("available", b->isCaseAvailable());
            caseObj.insert("level",    b->getCaseLevel());
            caseObj.insert("charging", b->isCaseCharging());
            caseObj.insert("optimized_charging", b->isCaseOptimizedCharging());
            status.insert("case", caseObj);
            // A Max reports one battery in Component::Headset, which left, right and case cannot express.
            QJsonObject headsetObj;
            headsetObj.insert("available", b->isHeadsetAvailable());
            headsetObj.insert("level",    b->getHeadsetLevel());
            headsetObj.insert("charging", b->isHeadsetCharging());
            status.insert("headset", headsetObj);
        }
        status.insert("reconnect_attempts_total", reconnectAttemptsTotal());
        status.insert("reconnect_failures_total", reconnectFailuresTotal());
        status.insert("noise_control_changes_total", noiseControlChangesTotal());
        status.insert("forget_calls_total", forgetCallsTotal());
        status.insert("ear_detection_changes_total", earDetectionChangesTotal());
        status.insert("ca_changes_total", caChangesTotal());
        status.insert("disconnect_calls_total", disconnectCallsTotal());
        status.insert("connect_calls_total", connectCallsTotal());
        status.insert("disconnect_failures_total", disconnectFailuresTotal());
        status.insert("connect_failures_total", connectFailuresTotal());
        status.insert("adaptive_level_changes_total", adaptiveLevelChangesTotal());
        status.insert("one_bud_anc_changes_total", oneBudANCChangesTotal());
        status.insert("reopen_calls_total", reopenCallsTotal());
        status.insert("conversational_awareness", d ? d->conversationalAwareness() : false);
        status.insert("adaptive_noise_level", d ? d->adaptiveNoiseLevel() : 0);
        status.insert("one_bud_anc_mode", d ? d->oneBudANCMode() : false);
        status.insert("model_name", d ? modelDisplayName(d->model()) : QString());
        status.insert("model_int", d ? static_cast<int>(d->model()) : 0);
        status.insert("is_pro_series", d ? isProSeriesAirPods(d->model()) : false);
        // The panel needs the shape before any battery packet has arrived.
        status.insert("is_headset", d ? isModelHeadset(d->model()) : false);
        status.insert("supports_noise_off", d ? supportsNoiseOff(d->model()) : true);
        // Additive keys: a panel that reads none of them keeps its is_pro_series behaviour.
        status.insert("supports_noise_control", d ? supportsNoiseControl(d->model()) : true);
        status.insert("supports_adaptive", d ? supportsAdaptiveAudio(d->model()) : false);
        status.insert("supports_conversational_awareness", d ? supportsConversationalAwareness(d->model()) : false);
        status.insert("supports_one_bud_anc", d ? supportsOneBudANC(d->model()) : false);
        // Raw "A<NNNN>" code from the AAP metadata packet. Useful
        // for debugging new/unrecognized models — if a fresh
        // device sends a code that isn't in parseModelNumber's
        // map, model_name will be empty but model_number stays
        // populated so the user can file the missing variant.
        status.insert("model_number", d ? d->modelNumber() : QString());
        status.insert("firmware_version", d ? d->firmwareVersion() : QString());
        status.insert("serial_number", d ? d->serialNumber() : QString());
        status.insert("hardware_revision", d ? d->hardwareRevision() : QString());
        status.insert("left_serial", d ? d->leftSerial() : QString());
        status.insert("right_serial", d ? d->rightSerial() : QString());
        status.insert("hearing_aid", d ? d->hearingAidEnabled() : false);
        // Evidence of which control commands echoed this session, not a list of settings.
        QJsonArray idsSeen;
        for (const quint8 id : m_controlCommands.idsSeen()) {
            idsSeen.append(QStringLiteral("0x%1").arg(id, 2, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("0X"), QStringLiteral("0x")));
        }
        status.insert("control_ids_seen", idsSeen);
        status.insert("setting_changes_total", m_settingChangesTotal);
        status.insert("notifications_enabled", m_notifier->enabled());
        status.insert("notifications_connected", loadConnectedBannerEnabled());
        status.insert("audio_follow_on_connect", mediaController->followOnConnect());
        QJsonObject audioSource;
        audioSource.insert("type", m_lastAudioSource ? OpenPods::AudioSource::typeName(m_lastAudioSource->type) : QStringLiteral("unknown"));
        audioSource.insert("other_device", m_lastAudioSourceOther);
        status.insert("audio_source", audioSource);
        status.insert("handoff_claims_total", m_handoff.claimsTotal());
        status.insert("handoff_interruptions_total", m_handoff.interruptionsTotal());
        status.insert("handoff_interrupted", m_handoff.interrupted());
        status.insert("handoff_connect_on_play", loadConnectOnPlay());
        status.insert("hearing_gate_ready", m_hearingGateReady);
        // This unit echoes 0x33 as 00, which is neither on nor off, so anything but 01 or 02 is published as null.
        if (const auto assist = currentSettingBytes(AirPodsPackets::HearingAssist::Type::ID); assist && !assist->isEmpty()) {
            const quint8 byte = static_cast<quint8>(assist->at(0));
            status.insert("hearing_assist", byte == 0x01 ? QJsonValue(true) : byte == 0x02 ? QJsonValue(false) : QJsonValue());
        }
        // CC 0x2C carries enrolled then enabled; the bool key above keeps its old meaning, so enrolment is its own key.
        if (const auto hearingAid = m_controlCommands.payload(hearingAidControlId); hearingAid && !hearingAid->isEmpty()) {
            status.insert("hearing_aid_enrolled", static_cast<quint8>(hearingAid->at(0)) == 0x01);
        }
        if (m_loudSoundReduction) {
            status.insert("loud_sound_reduction", *m_loudSoundReduction);
        }
        if (!m_transparencyCustom.isEmpty()) {
            status.insert("transparency_custom_hex", QString::fromLatin1(m_transparencyCustom.toHex()));
        }
        // Each setting reads from the echo when the pods sent one, else from the persisted wish; absent means neither.
        for (const OpenPods::PodSettings::Spec &spec : OpenPods::PodSettings::table()) {
            const std::optional<QByteArray> bytes = currentSettingBytes(spec.id);
            if (!bytes) {
                continue;
            }
            if (spec.kind == OpenPods::PodSettings::Kind::Sides) {
                const QJsonObject sides = OpenPods::PodSettings::decodeSides(*bytes);
                for (auto it = sides.begin(); it != sides.end(); ++it) {
                    status.insert(it.key(), it.value());
                }
            } else {
                status.insert(QString::fromLatin1(spec.statusKey), OpenPods::PodSettings::decode(spec, *bytes));
            }
        }
        if (m_settings) {
            const QString eqText = m_settings->value(QStringLiteral("CustomEq/request")).toString();
            if (const auto eq = OpenPods::PodSettings::parseEq(eqText)) {
                status.insert("custom_eq", OpenPods::PodSettings::eqJson(*eq));
            }
        }
        // 0 = PauseWhenOneRemoved, 1 = PauseWhenBothRemoved,
        // 2 = Disabled (matches MediaController::EarDetectionBehavior).
        status.insert("ear_detection_behavior", earDetectionBehavior());
        // 0 = open, 1 = closed, 2 = unknown (matches BleInfo::LidState).
        status.insert("lid_state", lidState());
        return status;
    }

    // Published on change for consumers that watch a file instead of polling a
    // process. QSaveFile renames into place, so a reader never sees half a line.
    void writeStateFile()
    {
        const QByteArray line = QJsonDocument(statusJson()).toJson(QJsonDocument::Compact) + "\n";
        if (line == m_lastState) {
            return;
        }
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
            + QStringLiteral("/librepods");
        if (!QDir().mkpath(dir)) {
            LOG_ERROR("Cannot create state directory: " << dir);
            return;
        }
        QSaveFile file(dir + QStringLiteral("/status.json"));
        if (!file.open(QIODevice::WriteOnly)) {
            LOG_ERROR("Cannot open state file: " << file.fileName());
            return;
        }
        // The mode goes on the open temporary the commit renames into place, never on the final path.
        if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            LOG_ERROR("Cannot restrict the state file mode, refusing to publish: " << file.fileName());
            return;
        }
        file.write(line);
        if (file.commit()) {
            m_lastState = line;
        }
    }

    // 0 open, 1 closed, 2 unknown, matching BleInfo::LidState.
    int lidState() const { return static_cast<int>(m_lidState); }
private:
    bool m_hideOnStart = false;
    QByteArray m_lastState;
    // Lid only moves on a BLE advertisement, so this stays UNKNOWN until one arrives.
    BleInfo::LidState m_lidState = BleInfo::LidState::UNKNOWN;
    DeviceInfo *m_deviceInfo;
    BleManager *m_bleManager;
    SystemSleepMonitor *m_systemSleepMonitor = nullptr;
    Notifier *m_notifier = nullptr;
    QString m_phoneMacStatus;
};

int main(int argc, char *argv[]) {
    // Read before the application object exists, because --headless decides which class to construct.
    bool debugMode = false;
    bool hideOnStart = false;
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--debug") {
            debugMode = true;
        }
        if (QString(argv[i]) == "--hide") {
            hideOnStart = true;
        }
        // --hide still builds the whole GUI so the window can be opened later; --headless never can.
        if (QString(argv[i]) == "--headless") {
            headless = true;
        }
    }

    // Set BEFORE QApplication construction so Qt's QDBusTrayIcon uses
    // "openpods" as the SNI Id property. Quickshell's Bar.qml matches on
    // this id to route left-click to PodsMenu instead of activating the
    // main window. QSettings/QStandardPaths are NOT bound to this name
    // (we use hardcoded "AirPodsTrayApp" in QSettings ctor), so flipping
    // the application name doesn't migrate user settings.
    QCoreApplication::setApplicationName("openpods");
    QCoreApplication::setOrganizationName("openpods");

    // Constructing QApplication is what pages most of Qt Gui, Widgets, Qml and Quick in; linking them still costs.
    std::unique_ptr<QCoreApplication> appOwner;
    if (headless) {
        appOwner = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        // QtWidgets fallback (any file dialog, message box, native QStyle) needs
        // a dark palette so it doesn't pop a white panel on top of our dark QML.
        // This must be set before QApplication constructor to be picked up by
        // QStyle on init.
        {
            QPalette p;
            p.setColor(QPalette::Window,          QColor("#0d0d0d"));
            p.setColor(QPalette::WindowText,      QColor("#ffffff"));
            p.setColor(QPalette::Base,            QColor("#1a1a1a"));
            p.setColor(QPalette::AlternateBase,   QColor("#141414"));
            p.setColor(QPalette::Text,            QColor("#ffffff"));
            p.setColor(QPalette::Button,          QColor("#1a1a1a"));
            p.setColor(QPalette::ButtonText,      QColor("#ffffff"));
            p.setColor(QPalette::Highlight,       QColor("#b6b6b6"));
            p.setColor(QPalette::HighlightedText, QColor("#0d0d0d"));
            p.setColor(QPalette::PlaceholderText, QColor("#8d8d8d"));
            QApplication::setPalette(p);
        }
        QQuickStyle::setStyle("Basic");
        appOwner = std::make_unique<QApplication>(argc, argv);
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }
    QCoreApplication &app = *appOwner;

    // POSIX signal → Qt event-loop bridge. Qt6 doesn't catch SIGTERM by
    // default, so systemd-style TERM kills the process without firing
    // aboutToQuit — the control socket lingers, ~AirPodsTrayApp doesn't run,
    // reliability counters never log their final summary.
    //
    // The classic async-signal-safe pattern: signal handler writes one
    // byte to a self-pipe; a QSocketNotifier in the GUI thread reads it
    // and calls QCoreApplication::quit() from a safe context.
    static int sigPipe[2] = { -1, -1 };
    if (::pipe(sigPipe) == 0) {
        // Set close-on-exec so child processes (e.g. bluetoothctl spawned
        // via QProcess) don't inherit the pipe.
        ::fcntl(sigPipe[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(sigPipe[1], F_SETFD, FD_CLOEXEC);

        struct sigaction sa{};
        sa.sa_handler = [](int){ char b = 1; ::write(sigPipe[1], &b, 1); };
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        ::sigaction(SIGTERM, &sa, nullptr);
        ::sigaction(SIGINT,  &sa, nullptr);
        ::sigaction(SIGHUP,  &sa, nullptr);

        auto *sigNotifier = new QSocketNotifier(sigPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(sigNotifier, &QSocketNotifier::activated, &app, [&app, sigNotifier]() {
            char b;
            if (::read(sigPipe[0], &b, 1) > 0) {
                LOG_INFO("Termination signal received, quitting");
            }
            sigNotifier->setEnabled(false);
            QCoreApplication::quit();
        });
    } else {
        LOG_WARN("Could not create signal pipe; SIGTERM will skip cleanup");
    }

    // Load translations. Stack-allocate so static analyzers can see the
    // lifetime; QApplication::installTranslator keeps an internal pointer
    // for the duration. translator lives until main returns, which is
    // also when app exec() returns — safe.
    QTranslator translator;
    QString locale = QLocale::system().name();

    // Try to load translation from various locations. OpenPods-prefix
    // paths are searched first so a future repackage to /usr/share/openpods
    // wins without removing the legacy librepods/ entries, which keeps
    // existing user installs working during the transition.
    QStringList translationPaths = {
        QCoreApplication::applicationDirPath() + "/translations",
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/openpods/translations",
        "/usr/share/openpods/translations",
        "/usr/local/share/openpods/translations",
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/librepods/translations",
        "/usr/share/librepods/translations",
        "/usr/local/share/librepods/translations"
    };

    for (const QString &path : translationPaths) {
        if (translator.load("openpods_" + locale, path)
            || translator.load("librepods_" + locale, path)) {
            app.installTranslator(&translator);
            break;
        }
    }

    // Refuse to run rather than fall back to /tmp, which is what this replaced.
    const QString ipcPath = OpenPods::Ipc::socketPath();
    if (ipcPath.isEmpty()) {
        LOG_ERROR("XDG_RUNTIME_DIR is unset; cannot place the control socket");
        return 1;
    }

    // Single-instance guard. Try to connect to an existing daemon's
    // QLocalServer first. If the connect succeeds, another instance
    // is alive — ask it to reopen its window + exit cleanly. Only
    // when the connect fails do we tear down the stale socket file
    // and bind our own server below.
    //
    // Order is load-bearing: removing the socket file before the
    // connect attempt (the previous behavior) made the guard a
    // no-op — connectToServer can't reach a unix socket whose file
    // we already deleted, so every launch spawned a fresh daemon.
    // That manifested as walker / desktop-file double-click
    // spawning new instances instead of surfacing the existing
    // window.
    {
        QLocalSocket socket_check;
        socket_check.connectToServer(ipcPath);
        if (socket_check.waitForConnected(300)) {
            LOG_INFO("Another instance already running! Reopening window...");
            socket_check.write("reopen");
            socket_check.flush();
            socket_check.waitForBytesWritten(200);
            socket_check.disconnectFromServer();
            return 0;
        }
    }

    // No existing daemon answered, so unlink the socket a crashed-or-SIGKILL'd
    // predecessor left behind before binding our own.
    QLocalServer::removeServer(ipcPath);

    if (!headless) {
        QGuiApplication::setDesktopFileName("me.kavishdevar.librepods");
        QGuiApplication::setQuitOnLastWindowClosed(false);
    }

    // Same lifetime as the stack object this replaced, so the engine still deletes trayApp on the way out.
    std::unique_ptr<QQmlApplicationEngine> engineOwner;
    if (!headless) {
        engineOwner = std::make_unique<QQmlApplicationEngine>();
        qmlRegisterType<Battery>("me.kavishdevar.Battery", 1, 0, "Battery");
        qmlRegisterType<DeviceInfo>("me.kavishdevar.DeviceInfo", 1, 0, "DeviceInfo");
    }
    QQmlApplicationEngine *engine = engineOwner.get();

    AirPodsTrayApp *trayApp = new AirPodsTrayApp(debugMode, hideOnStart, headless, engine);
    // Headless has no engine to be parented to, and the destructor logs the final reliability summary.
    if (headless) {
        trayApp->setParent(&app);
    }

    if (!headless) {
        engine->rootContext()->setContextProperty("airPodsTrayApp", trayApp);

        // Expose PHONE_MAC_ADDRESS environment variable to QML for placeholder in settings
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            QString phoneMacEnv = env.value("PHONE_MAC_ADDRESS", "");
            engine->rootContext()->setContextProperty("PHONE_MAC_ADDRESS", phoneMacEnv);
            // Initialize the visible status in the GUI
            trayApp->updatePhoneMacStatus(phoneMacEnv.isEmpty() ? QStringLiteral("No phone MAC set") : phoneMacEnv);
        }

        engine->addImageProvider("qrcode", new QRCodeImageProvider());
    }

    // Defer Main.qml load when --hide. The daemon's tray icon + low-
    // battery toast + status IPC + AAP/BLE paths are all pure C++ —
    // no QML root needed until the user explicitly opens the window.
    // Loading Main.qml eagerly costs ~60-100 MB of QML engine
    // overhead (Qt's QML runtime + Material/Basic style + every
    // imported singleton instantiated). On a --hide autostart the
    // user may never open the window in a session, so eager load is
    // pure waste. onOpenApp / onOpenSettings / the "reopen" IPC verb
    // all check rootObjects() and lazy-load on demand.
    if (!headless && !hideOnStart) {
        trayApp->loadMainModule();
    }

    // One second is far below any rate a human notices and far above the cost of
    // serialising a small object in process, which is what this replaces polling with.
    QTimer stateWriter;
    stateWriter.setInterval(1000);
    QObject::connect(&stateWriter, &QTimer::timeout, trayApp, [trayApp]() { trayApp->writeStateFile(); });
    stateWriter.start();

    QLocalServer server;
    QLocalServer::removeServer(ipcPath);
    // Qt binds the socket at 0777 minus the umask unless told otherwise, and listen() is what applies this.
    server.setSocketOptions(QLocalServer::UserAccessOption);

    if (!server.listen(ipcPath))
    {
        LOG_ERROR("Unable to start the listening server");
        LOG_DEBUG("Server error: " << server.errorString());
    }
    else
    {
        LOG_DEBUG("Server started, waiting for connections...");
    }
    // Capture by pointer (not reference-to-local) so the lambdas don't
    // dangle if main()'s frame is unwound by an exception or alt exit
    // path. `engine` and `trayApp` outlive the server (they're created
    // earlier), and `serverPtr` lets us avoid `[&]` capturing the local.
    QLocalServer *serverPtr = &server;
    QQmlApplicationEngine *enginePtr = engine;
    AirPodsTrayApp *trayAppPtr = trayApp;

    QObject::connect(serverPtr, &QLocalServer::newConnection,
                     serverPtr, [serverPtr, enginePtr, trayAppPtr]() {
        QLocalSocket* clientSocket = serverPtr->nextPendingConnection();
        if (!clientSocket) return;

        // Reparent to server so it dies with us if not cleaned up sooner.
        clientSocket->setParent(serverPtr);

        QObject::connect(clientSocket, &QLocalSocket::readyRead,
                         clientSocket, [clientSocket, enginePtr, trayAppPtr]() {
            const QString msg = clientSocket->readAll();
            // Syntax lives in verbtable.hpp; a refusal from a setter is the reason the pods or the model gave.
            const OpenPods::Ipc::Parsed parsed = OpenPods::Ipc::parseVerb(msg);
            if (!parsed.ok) {
                LOG_ERROR("Rejected IPC message: " << msg);
                clientSocket->write(parsed.reply);
                clientSocket->flush();
                clientSocket->disconnectFromServer();
                return;
            }
            QString refusal;
            if (parsed.verb == QLatin1String("reopen")) {
                trayAppPtr->incReopenCallsTotal();
                // A headless daemon has no window, and the caller deserves an answer rather than silence.
                if (!enginePtr) {
                    LOG_WARN("Refusing reopen: this daemon runs headless");
                    refusal = QStringLiteral("this daemon runs headless and has no window");
                } else {
                    LOG_INFO("Reopening app window");
                    const auto roots = enginePtr->rootObjects();
                    if (!roots.isEmpty()) {
                        QMetaObject::invokeMethod(roots.first(), "reopen", Q_ARG(QVariant, "app"));
                    } else {
                        trayAppPtr->loadMainModule();
                    }
                }
            } else if (parsed.verb == QLatin1String("status")) {
                // Schema is additive: never rename or remove a key without bumping schema_version.
                const QJsonObject status = trayAppPtr->statusJson();
                const QByteArray line = QJsonDocument(status).toJson(QJsonDocument::Compact) + "\n";
                clientSocket->write(line);
                clientSocket->flush();
                clientSocket->disconnectFromServer();
                return;
            } else if (parsed.verb == QLatin1String("noise")) {
                refusal = parsed.choice == QLatin1String("cycle") ? trayAppPtr->cycleNoiseControlMode()
                                                                   : trayAppPtr->setNoiseControlModeInt(parsed.number);
            } else if (parsed.verb == QLatin1String("ear")) {
                // The choice order off, one, both maps onto Disabled, PauseWhenOneRemoved, PauseWhenBothRemoved.
                static const MediaController::EarDetectionBehavior behaviors[] = {
                    MediaController::EarDetectionBehavior::Disabled,
                    MediaController::EarDetectionBehavior::PauseWhenOneRemoved,
                    MediaController::EarDetectionBehavior::PauseWhenBothRemoved};
                trayAppPtr->setEarDetectionBehavior(static_cast<int>(behaviors[parsed.number]));
            } else if (parsed.verb == QLatin1String("forget")) {
                refusal = trayAppPtr->forgetDevice();
            } else if (parsed.verb == QLatin1String("ca")) {
                refusal = trayAppPtr->setConversationalAwareness(parsed.choice == QLatin1String("on"));
            } else if (parsed.verb == QLatin1String("disconnect")) {
                refusal = trayAppPtr->disconnectAirPods();
            } else if (parsed.verb == QLatin1String("connect")) {
                refusal = trayAppPtr->connectAirPods();
            } else if (parsed.verb == QLatin1String("onebud")) {
                refusal = trayAppPtr->setOneBudANCMode(parsed.choice == QLatin1String("on"));
            } else if (parsed.verb == QLatin1String("adaptive")) {
                refusal = trayAppPtr->setAdaptiveNoiseLevel(parsed.number);
            } else if (parsed.verb == QLatin1String("rename")) {
                refusal = trayAppPtr->renameAirPods(parsed.text);
            } else if (parsed.verb == QLatin1String("eq")) {
                refusal = trayAppPtr->setCustomEq(parsed.text);
            } else if (parsed.verb == QLatin1String("notify")) {
                const bool on = parsed.choice.endsWith(QLatin1String("on"));
                if (parsed.choice.startsWith(QLatin1String("connected:"))) {
                    trayAppPtr->saveConnectedBannerEnabled(on);
                } else {
                    trayAppPtr->setNotificationsEnabled(on);
                }
            } else if (parsed.verb == QLatin1String("follow")) {
                trayAppPtr->saveFollowOnConnect(parsed.choice == QLatin1String("on"));
            } else if (parsed.verb == QLatin1String("handoff")) {
                trayAppPtr->saveConnectOnPlay(parsed.choice.endsWith(QLatin1String(":on")));
            } else if (parsed.verb == QLatin1String("hearingaid")) {
                refusal = trayAppPtr->setHearingAidEnabled(parsed.choice == QLatin1String("on"));
            } else if (parsed.verb == QLatin1String("hearingassist")) {
                refusal = trayAppPtr->setHearingAssist(parsed.choice == QLatin1String("on"));
            } else if (parsed.verb == QLatin1String("lsr")) {
                refusal = trayAppPtr->setLoudSoundReduction(parsed.choice == QLatin1String("on"));
            } else if (const auto spec = OpenPods::PodSettings::forVerb(parsed.verb)) {
                refusal = trayAppPtr->setPodSetting(*spec, parsed);
            } else {
                // The table and this adapter must list the same families, so a miss here is a build defect, not a user error.
                LOG_ERROR("Verb has a table row but no handler: " << parsed.verb);
                refusal = QStringLiteral("verb %1 has no handler in this daemon").arg(parsed.verb);
            }
            if (refusal.isEmpty()) {
                clientSocket->write("ok\n");
            } else {
                LOG_WARN("Refused " << msg << ": " << refusal);
                clientSocket->write(QStringLiteral("error: %1\n").arg(refusal).toUtf8());
            }
            clientSocket->flush();
            clientSocket->disconnectFromServer();
        });
        QObject::connect(clientSocket, &QLocalSocket::errorOccurred,
                         clientSocket, [clientSocket](QLocalSocket::LocalSocketError) {
            LOG_ERROR("Local socket error: " << clientSocket->errorString());
        });
        // Free the QLocalSocket once the peer hangs up — without this
        // every reopen ping leaks one socket for the lifetime of the OpenPods daemon.
        QObject::connect(clientSocket, &QLocalSocket::disconnected,
                         clientSocket, &QLocalSocket::deleteLater);
    });

    // No error handler here: newConnection is QLocalServer's only signal, so the failed listen above is the only error it reports.

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     serverPtr, [serverPtr, ipcPath]() {
        LOG_DEBUG("Application quitting. Cleaning up local server...");
        if (serverPtr->isListening()) {
            serverPtr->close();
        }
        QLocalServer::removeServer(ipcPath);
        // An absent state file is how a watcher learns the daemon stopped.
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
                      + QStringLiteral("/librepods/status.json"));
    });
    // QApplication::exec sets the accessibility root, which QCoreApplication::exec does not.
    return headless ? QCoreApplication::exec() : QApplication::exec();
}

#include "main.moc"
