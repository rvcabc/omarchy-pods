#include <QCoreApplication>
#include <QFileInfo>
#include <QLocalSocket>
#include <QTextStream>

#include "ipcpath.hpp"
#include "verbtable.hpp"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        // Self-name: use argv[0] basename so a symlink (openpods-ctl)
        // shows the right usage line. Falls back to "openpods-ctl" if
        // QCoreApplication can't tell us its file path.
        const QString self = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        const QString prog = self.isEmpty() ? QStringLiteral("openpods-ctl") : self;
        QTextStream err(stderr);
        err << "Usage: " << prog << " <command>\n" << "Commands:\n";
        // One line per verb table row, so the usage text and the daemon can never disagree.
        for (const QString &line : OpenPods::Ipc::usageLines()) {
            err << line << "\n";
        }
        return 1;
    }

    const QByteArray cmd = QByteArray(argv[1]);
    // status is the one verb whose reply carries data; every other verb answers ok or error, or closes quietly.
    const bool wantsStatus = (cmd == "status");

    const QString ipcPath = OpenPods::Ipc::socketPath();
    if (ipcPath.isEmpty()) {
        QTextStream(stderr) << "XDG_RUNTIME_DIR is unset; cannot locate the daemon socket\n";
        return 1;
    }

    QLocalSocket socket;
    socket.connectToServer(ipcPath);

    if (!socket.waitForConnected(500)) {
        QTextStream(stderr) << "Could not connect to OpenPods daemon at " << ipcPath
                            << " (is it running?)\n";
        return 1;
    }

    socket.write(cmd);
    socket.flush();
    // 200ms was tight: on a slow host (especially under valgrind/ASAN)
    // the daemon hadn't drained the write before disconnectFromServer
    // tore the socket down, silently dropping the command. 500ms gives
    // headroom without making the CLI feel laggy.
    socket.waitForBytesWritten(500);

    // The daemon writes one line then half-closes. Wait briefly for it, otherwise the read returns empty.
    if (!socket.waitForReadyRead(1000)) {
        // A clean close with no bytes is a yes from a daemon that predates the reply contract, and from a
        // windowed daemon answering reopen; status alone must carry data, so its silence is a failure.
        if (!wantsStatus && socket.state() != QLocalSocket::ConnectedState) {
            socket.disconnectFromServer();
            return 0;
        }
        QTextStream(stderr) << "Timed out waiting for a reply to " << cmd << "\n";
        socket.disconnectFromServer();
        return 1;
    }
    const QByteArray reply = socket.readAll();
    if (reply.startsWith("error:")) {
        QTextStream(stderr) << QString::fromUtf8(reply);
        socket.disconnectFromServer();
        return 1;
    }
    // ok is the whole answer for a control verb, so only a status object reaches stdout.
    if (wantsStatus || reply.trimmed() != "ok") {
        QTextStream(stdout) << QString::fromUtf8(reply);
    }

    socket.disconnectFromServer();
    return 0;
}
