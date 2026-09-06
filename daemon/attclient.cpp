#include "attclient.h"
#include "logger.h"

#include <QSocketNotifier>

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
// bdaddr_t stores the address little-endian, so "14:1A:97:54:1E:3A" becomes 3a 1e 54 97 1a 14.
bool macToBdaddr(const QString &macAddress, bdaddr_t *out)
{
    const QStringList parts = macAddress.split(QLatin1Char(':'));
    if (parts.size() != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        const uint value = parts.at(i).toUInt(&ok, 16);
        if (!ok || value > 0xFF) {
            return false;
        }
        out->b[5 - i] = static_cast<uint8_t>(value);
    }
    return true;
}

// PDUs on this channel are a few bytes; the transparency blob is the largest at well under this.
constexpr int readBufferBytes = 512;
}

AttClient::AttClient(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        failPending(QStringLiteral("no ATT reply within %1 ms").arg(OpenPods::Att::requestTimeoutMs));
    });
}

AttClient::~AttClient()
{
    close();
}

bool AttClient::open(const QString &macAddress, QString *reason)
{
    close();
    bdaddr_t target{};
    if (!macToBdaddr(macAddress, &target)) {
        if (reason) *reason = QStringLiteral("not a Bluetooth address: %1").arg(macAddress);
        return false;
    }
    m_fd = ::socket(AF_BLUETOOTH, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, BTPROTO_L2CAP);
    if (m_fd < 0) {
        if (reason) *reason = QStringLiteral("socket(AF_BLUETOOTH, L2CAP) failed: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    sockaddr_l2 address{};
    address.l2_family = AF_BLUETOOTH;
    address.l2_psm = htobs(OpenPods::Att::psm);
    address.l2_bdaddr = target;
    address.l2_bdaddr_type = BDADDR_BREDR;
    if (::connect(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 && errno != EINPROGRESS) {
        if (reason) *reason = QStringLiteral("connect to PSM %1 failed: %2").arg(OpenPods::Att::psm).arg(QString::fromLocal8Bit(strerror(errno)));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    m_writeNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Write, this);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &AttClient::onWritable);
    m_readNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &AttClient::onReadable);
    m_readNotifier->setEnabled(false);
    LOG_INFO("ATT channel connecting to PSM " << OpenPods::Att::psm);
    return true;
}

void AttClient::close()
{
    if (m_fd < 0) {
        return;
    }
    m_timeout.stop();
    failPending(QStringLiteral("ATT channel closed"));
    for (const Pending &pending : m_queue) {
        if (pending.done) pending.done(false, QStringLiteral("ATT channel closed"), QByteArray());
    }
    m_queue.clear();
    delete m_readNotifier;
    delete m_writeNotifier;
    m_readNotifier = nullptr;
    m_writeNotifier = nullptr;
    ::close(m_fd);
    m_fd = -1;
    m_connected = false;
}

void AttClient::onWritable()
{
    // The write notifier fires once when the non-blocking connect settles; SO_ERROR says how.
    int error = 0;
    socklen_t length = sizeof(error);
    if (::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {
        error = errno;
    }
    m_writeNotifier->setEnabled(false);
    if (error != 0) {
        const QString reason = QStringLiteral("ATT connect failed: %1").arg(QString::fromLocal8Bit(strerror(error)));
        LOG_ERROR(reason);
        close();
        emit closed(reason);
        return;
    }
    m_connected = true;
    m_readNotifier->setEnabled(true);
    LOG_INFO("ATT channel connected");
    sendNext();
}

void AttClient::onReadable()
{
    char buffer[readBufferBytes];
    const ssize_t received = ::recv(m_fd, buffer, sizeof(buffer), 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }
        const QString reason = QStringLiteral("ATT read failed: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        LOG_ERROR(reason);
        close();
        emit closed(reason);
        return;
    }
    if (received == 0) {
        LOG_WARN("ATT channel closed by the pods");
        close();
        emit closed(QStringLiteral("ATT channel closed by the pods"));
        return;
    }
    const QByteArray pdu(buffer, static_cast<int>(received));
    const OpenPods::Att::Reply reply = OpenPods::Att::parseReply(pdu);
    LOG_DEBUG("ATT received: " << pdu.toHex() << " (" << reply.describe() << ")");
    if (reply.kind == OpenPods::Att::Reply::Kind::Notification) {
        emit notification(reply.handle, reply.value);
        return;
    }
    if (!m_inFlight) {
        LOG_WARN("Unsolicited " << reply.describe());
        return;
    }
    if (reply.kind == OpenPods::Att::Reply::Kind::Error) {
        finishPending(false, reply.describe(), QByteArray());
    } else if (reply.kind == OpenPods::Att::Reply::Kind::Unknown) {
        finishPending(false, reply.describe(), pdu);
    } else {
        finishPending(true, reply.describe(), reply.value);
    }
}

void AttClient::read(quint16 handle, Callback done)
{
    m_queue.append({OpenPods::Att::readRequest(handle), std::move(done)});
    sendNext();
}

void AttClient::write(quint16 handle, const QByteArray &value, Callback done)
{
    m_queue.append({OpenPods::Att::writeRequest(handle, value), std::move(done)});
    sendNext();
}

void AttClient::sendNext()
{
    if (m_inFlight || m_queue.isEmpty() || !isOpen()) {
        return;
    }
    const Pending &next = m_queue.first();
    LOG_DEBUG("ATT sending: " << next.pdu.toHex());
    const ssize_t sent = ::send(m_fd, next.pdu.constData(), static_cast<size_t>(next.pdu.size()), 0);
    if (sent < 0) {
        const QString reason = QStringLiteral("ATT send failed: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        LOG_ERROR(reason);
        Pending failed = m_queue.takeFirst();
        if (failed.done) failed.done(false, reason, QByteArray());
        return;
    }
    m_inFlight = true;
    m_timeout.start(OpenPods::Att::requestTimeoutMs);
}

void AttClient::failPending(const QString &reason)
{
    if (!m_inFlight) {
        return;
    }
    finishPending(false, reason, QByteArray());
}

void AttClient::finishPending(bool ok, const QString &detail, const QByteArray &value)
{
    m_timeout.stop();
    m_inFlight = false;
    if (m_queue.isEmpty()) {
        return;
    }
    Pending done = m_queue.takeFirst();
    if (done.done) done.done(ok, detail, value);
    sendNext();
}
