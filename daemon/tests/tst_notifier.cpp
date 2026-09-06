// Which of the two paths a given PATH and enabled state picks, since headless has no tray, and the exact argv each toast sends.

#include "notifier.hpp"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestNotifier : public QObject
{
    Q_OBJECT

    // notification, send, --app-name, AirPods, -g, the glyph, -p, title, message.
    static constexpr int kExpectedArgCount = 9;

private slots:
    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_argsFile = m_dir->path() + QStringLiteral("/args");
        qputenv("PATH", m_dir->path().toLocal8Bit());
    }

    void omarchyOnPath_isSpawnedWithTheArguments()
    {
        writeFakeOmarchy();
        Notifier notifier;
        QSignalSpy fallback(&notifier, &Notifier::fallbackRequested);

        notifier.notify(Notifier::Channel::LowBatteryLeft, QStringLiteral("Left AirPod Low Battery"), QStringLiteral("10% remaining"));

        // The stub's redirect creates the file before printf fills it, so wait on the content.
        QTRY_VERIFY_WITH_TIMEOUT(readArgs().size() == kExpectedArgCount, 3000);
        const QStringList args = readArgs();
        QCOMPARE(args.value(0), QStringLiteral("notification"));
        QCOMPARE(args.value(1), QStringLiteral("send"));
        QCOMPARE(args.value(2), QStringLiteral("--app-name"));
        QCOMPARE(args.value(3), QStringLiteral("AirPods"));
        QCOMPARE(args.value(6), QStringLiteral("-p"));
        QCOMPARE(args.value(7), QStringLiteral("Left AirPod Low Battery"));
        QCOMPARE(args.value(8), QStringLiteral("10% remaining"));
        // The tray fallback is for when omarchy is absent, so spawning it must not also fire.
        QCOMPARE(fallback.count(), 0);
    }

    void omarchyMissing_asksForTheTrayFallback()
    {
        Notifier notifier;
        QSignalSpy fallback(&notifier, &Notifier::fallbackRequested);

        notifier.notify(Notifier::Channel::Disconnected, QStringLiteral("AirPods Disconnected"), QStringLiteral("bye"));

        QCOMPARE(fallback.count(), 1);
        QCOMPARE(fallback.first().at(0).toString(), QStringLiteral("AirPods Disconnected"));
        QCOMPARE(fallback.first().at(1).toString(), QStringLiteral("bye"));
    }

    void disabled_doesNeither()
    {
        writeFakeOmarchy();
        Notifier notifier;
        notifier.setEnabled(false);
        QSignalSpy fallback(&notifier, &Notifier::fallbackRequested);

        notifier.notify(Notifier::Channel::LowBatteryLeft, QStringLiteral("Left AirPod Low Battery"), QStringLiteral("10% remaining"));

        QTest::qWait(300);
        QVERIFY(!QFile::exists(m_argsFile));
        QCOMPARE(fallback.count(), 0);
    }

    // The whole argv is pinned: -u and -t only when asked, -r only with an id, -p always, --exec last.
    void arguments_pinTheOrderTheCliParses()
    {
        Notifier::Options options;
        options.urgency = QStringLiteral("low");
        options.timeoutMs = 5000;
        options.execArgv = {QStringLiteral("omarchy-shell"), QStringLiteral("omapods"), QStringLiteral("open")};
        const QStringList withId = Notifier::arguments(QStringLiteral("AirPods Pro connected"),
                                                       QStringLiteral("Left 93%  Right 89%"), options, 42);
        const QStringList expected = {
            QStringLiteral("notification"), QStringLiteral("send"), QStringLiteral("--app-name"), QStringLiteral("AirPods"),
            QStringLiteral("-g"), QString::fromUtf8(Notifier::glyph), QStringLiteral("-u"), QStringLiteral("low"),
            QStringLiteral("-t"), QStringLiteral("5000"), QStringLiteral("-r"), QStringLiteral("42"), QStringLiteral("-p"),
            QStringLiteral("AirPods Pro connected"), QStringLiteral("Left 93%  Right 89%"),
            QStringLiteral("--exec"), QStringLiteral("omarchy-shell"), QStringLiteral("omapods"), QStringLiteral("open")};
        QCOMPARE(withId, expected);

        const QStringList bare = Notifier::arguments(QStringLiteral("T"), QStringLiteral("M"), Notifier::Options(), Notifier::noReplaceId);
        QCOMPARE(bare, QStringList({QStringLiteral("notification"), QStringLiteral("send"), QStringLiteral("--app-name"),
                                    QStringLiteral("AirPods"), QStringLiteral("-g"), QString::fromUtf8(Notifier::glyph),
                                    QStringLiteral("-p"), QStringLiteral("T"), QStringLiteral("M")}));
    }

    void batteryBanner_skipsWhatIsNotThere()
    {
        QCOMPARE(Notifier::batteryBanner(true, 93, true, 89, true, 100), QStringLiteral("Left 93%  Right 89%  Case 100%"));
        // The case says nothing while the pods are worn, which is when the connect banner fires.
        QCOMPARE(Notifier::batteryBanner(true, 93, true, 89, false, 0), QStringLiteral("Left 93%  Right 89%"));
        QCOMPARE(Notifier::batteryBanner(false, 0, true, 89, false, 0), QStringLiteral("Right 89%"));
        QCOMPARE(Notifier::batteryBanner(false, 0, false, 0, false, 0), QString());
    }

    // The stub prints the id the way omarchy-notification-send -p does, and the next toast on that channel carries -r.
    void printedId_isReusedOnTheSameChannelOnly()
    {
        writeFakeOmarchy(QStringLiteral("7"));
        Notifier notifier;

        notifier.notify(Notifier::Channel::Connected, QStringLiteral("first"), QStringLiteral("body"));
        QTRY_COMPARE_WITH_TIMEOUT(notifier.replaceIdFor(Notifier::Channel::Connected), 7u, 3000);
        QCOMPARE(notifier.replaceIdFor(Notifier::Channel::Disconnected), Notifier::noReplaceId);

        QFile::remove(m_argsFile);
        notifier.notify(Notifier::Channel::Connected, QStringLiteral("second"), QStringLiteral("body"));
        QTRY_VERIFY_WITH_TIMEOUT(readArgs().contains(QStringLiteral("-r")), 3000);
        const QStringList args = readArgs();
        QCOMPARE(args.value(args.indexOf(QStringLiteral("-r")) + 1), QStringLiteral("7"));
    }

    void enabledChanged_onlyFiresOnAChange()
    {
        Notifier notifier;
        QSignalSpy changed(&notifier, &Notifier::enabledChanged);

        notifier.setEnabled(true);
        QCOMPARE(changed.count(), 0);
        notifier.setEnabled(false);
        QCOMPARE(changed.count(), 1);
        notifier.setEnabled(false);
        QCOMPARE(changed.count(), 1);
    }

    void cleanup()
    {
        qunsetenv("PATH");
        m_dir.reset();
    }

private:
    // printedId is what the stub writes to stdout, the way the real CLI answers -p.
    void writeFakeOmarchy(const QString &printedId = QString())
    {
        const QString path = m_dir->path() + QStringLiteral("/omarchy");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"" + m_argsFile.toLocal8Bit() + "\"\n");
        if (!printedId.isEmpty()) {
            f.write("printf '%s\\n' '" + printedId.toLocal8Bit() + "'\n");
        }
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    }

    // One argument per line: "notification\nsend\n--app-name\nAirPods\n-g\n\uF025\nTitle\nMessage\n"
    QStringList readArgs()
    {
        QFile f(m_argsFile);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    QScopedPointer<QTemporaryDir> m_dir;
    QString m_argsFile;
};

QTEST_GUILESS_MAIN(TestNotifier)
#include "tst_notifier.moc"
