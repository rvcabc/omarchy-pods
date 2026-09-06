#pragma once

#include "attpdu.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

class QSocketNotifier;

// A raw L2CAP socket to the pods' ATT channel. Qt's D-Bus Bluetooth backend refuses to connect by PSM, which is why
// this is a POSIX socket wrapped in QSocketNotifier rather than a QBluetoothSocket. One request is outstanding at a
// time; every request answers within Att::requestTimeoutMs or fails loud.
class AttClient : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(bool ok, const QString &detail, const QByteArray &value)>;

    explicit AttClient(QObject *parent = nullptr);
    ~AttClient() override;

    // Connects to macAddress ("14:1A:97:54:1E:3A") on PSM 31; false with a reason when the socket cannot even be opened.
    bool open(const QString &macAddress, QString *reason);
    void close();
    bool isOpen() const { return m_fd >= 0 && m_connected; }
    bool isConnecting() const { return m_fd >= 0 && !m_connected; }

    void read(quint16 handle, Callback done);
    void write(quint16 handle, const QByteArray &value, Callback done);

signals:
    void notification(quint16 handle, const QByteArray &value);
    void closed(const QString &reason);

private:
    struct Pending
    {
        QByteArray pdu;
        Callback done;
    };
    void sendNext();
    void onReadable();
    void onWritable();
    void failPending(const QString &reason);
    void finishPending(bool ok, const QString &detail, const QByteArray &value);

    int m_fd = -1;
    bool m_connected = false;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer m_timeout;
    QList<Pending> m_queue;
    bool m_inFlight = false;
};
