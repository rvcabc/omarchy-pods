// The sticky allowlist, the QSettings store behind it and the re-send decision, on a throwaway ini file.

#include "settingsreassert.hpp"

#include <QtTest/QtTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

class TestSettingsReassert : public QObject
{
    Q_OBJECT

    // Both echo back after a connect and must never be re-sent from the echo set alone.
    static constexpr quint8 listeningModeId = 0x0D;
    static constexpr quint8 ownsConnectionId = 0x06;

    // 04 00 04 00 09 00 [id] [d1] [d2] [d3] [d4]
    static constexpr int controlFrameSize = 11;

private slots:
    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_iniPath = m_dir->path() + QStringLiteral("/settings.ini");
    }

    void cleanup()
    {
        m_dir.reset();
    }

    void onlyTheAllowlistIsSticky()
    {
        QVERIFY(OpenPods::Reassert::isSticky(0x1A));
        QVERIFY(OpenPods::Reassert::isSticky(0x34));
        QVERIFY(!OpenPods::Reassert::isSticky(listeningModeId));
        QVERIFY(!OpenPods::Reassert::isSticky(ownsConnectionId));
    }

    void saveThenLoad_roundTripsThroughTheIniFile()
    {
        {
            QSettings settings(m_iniPath, QSettings::IniFormat);
            OpenPods::Reassert::saveDesired(settings, 0x1A, QByteArray::fromHex("07"));
        }
        // QSettings serves a path it has already parsed from a per-process cache, so only a path it has never seen is read off the disk.
        const QString copyPath = m_dir->path() + QStringLiteral("/copy.ini");
        QVERIFY(QFile::copy(m_iniPath, copyPath));
        const QSettings reopened(copyPath, QSettings::IniFormat);
        const auto loaded = OpenPods::Reassert::loadDesired(reopened, 0x1A);
        QCOMPARE(loaded.value_or(QByteArray()), QByteArray::fromHex("07"));
    }

    void loadOfAnUnsavedId_returnsNullopt()
    {
        const QSettings settings(m_iniPath, QSettings::IniFormat);
        QVERIFY(!OpenPods::Reassert::loadDesired(settings, 0x34).has_value());
    }

    void loadOfAPlainStringValue_returnsNullopt()
    {
        // A hand edit: "07" would come back as the ASCII bytes 30 37.
        writeIni("[PodSettings]\n1a=07\n");
        const QSettings settings(m_iniPath, QSettings::IniFormat);
        QVERIFY(settings.value(QStringLiteral("PodSettings/1a")).isValid());
        QVERIFY(!OpenPods::Reassert::loadDesired(settings, 0x1A).has_value());
    }

    void loadOfAnEmptyByteArray_returnsNullopt()
    {
        writeIni("[PodSettings]\n1a=@ByteArray()\n");
        const QSettings settings(m_iniPath, QSettings::IniFormat);
        QVERIFY(settings.value(QStringLiteral("PodSettings/1a")).isValid());
        QVERIFY(!OpenPods::Reassert::loadDesired(settings, 0x1A).has_value());
    }

    void saveWritesGroupPodSettingsKeyedByTwoLowercaseHexDigits()
    {
        QSettings settings(m_iniPath, QSettings::IniFormat);
        OpenPods::Reassert::saveDesired(settings, 0x1A, QByteArray::fromHex("07"));

        // "[PodSettings]\n1a=@ByteArray(\a)\n"
        QFile f(m_iniPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(lines.contains(QStringLiteral("[PodSettings]")));
        bool found = false;
        for (const QString &line : lines) {
            found = found || line.startsWith(QStringLiteral("1a=@ByteArray("));
        }
        QVERIFY(found);
    }

    void desiredIds_listsSavedIdsAscending()
    {
        QSettings settings(m_iniPath, QSettings::IniFormat);
        OpenPods::Reassert::saveDesired(settings, 0x34, QByteArray::fromHex("01"));
        OpenPods::Reassert::saveDesired(settings, 0x1A, QByteArray::fromHex("07"));

        const QSettings reopened(m_iniPath, QSettings::IniFormat);
        QCOMPARE(OpenPods::Reassert::desiredIds(reopened), (QList<quint8>{0x1A, 0x34}));
    }

    void desiredIds_isEmptyOnAFreshStore()
    {
        const QSettings settings(m_iniPath, QSettings::IniFormat);
        QVERIFY(OpenPods::Reassert::desiredIds(settings).isEmpty());
    }

    void desiredIds_skipsKeysNotShapedLikeSaveDesiredWrites()
    {
        // The ini is case-sensitive here, so loadDesired could never find "1A" or "+1" under their lowercase names.
        writeIni("[PodSettings]\n1A=@ByteArray(\\x7)\n+1=@ByteArray(\\x1)\n34=@ByteArray(\\x1)\n");
        const QSettings settings(m_iniPath, QSettings::IniFormat);
        QVERIFY(settings.allKeys().contains(QStringLiteral("PodSettings/1A")));
        QVERIFY(settings.allKeys().contains(QStringLiteral("PodSettings/+1")));
        QCOMPARE(OpenPods::Reassert::desiredIds(settings), (QList<quint8>{0x34}));
    }

    void nonStickyId_isNeverReasserted()
    {
        QVERIFY(!OpenPods::Reassert::reassertFor(listeningModeId, QByteArray::fromHex("02"),
                                                 QByteArray::fromHex("03"))
                     .has_value());
        QVERIFY(!OpenPods::Reassert::reassertFor(ownsConnectionId, QByteArray::fromHex("01"),
                                                 std::nullopt)
                     .has_value());
    }

    void mismatch_buildsTheFrame()
    {
        const auto frame = OpenPods::Reassert::reassertFor(0x1A, QByteArray::fromHex("07"),
                                                           QByteArray::fromHex("0f"));
        QCOMPARE(frame.value_or(QByteArray()), QByteArray::fromHex("0400040009001A07000000"));
    }

    void noEcho_buildsTheFrame()
    {
        const auto frame = OpenPods::Reassert::reassertFor(0x34, QByteArray::fromHex("01"),
                                                           std::nullopt);
        QCOMPARE(frame.value_or(QByteArray()), QByteArray::fromHex("0400040009003401000000"));
    }

    void emptyDesired_isNeverSentAsFourZeroBytes()
    {
        QVERIFY(!OpenPods::Reassert::reassertFor(0x1A, QByteArray(), std::nullopt).has_value());
        QVERIFY(!OpenPods::Reassert::reassertFor(0x1A, QByteArray(), QByteArray::fromHex("0f000000"))
                     .has_value());
    }

    void exactMatch_sendsNothing()
    {
        QVERIFY(!OpenPods::Reassert::reassertFor(0x1A, QByteArray::fromHex("07"),
                                                 QByteArray::fromHex("07"))
                     .has_value());
    }

    void oneByteRequest_matchesItsFourByteEcho()
    {
        // The echo is read off an 11-byte frame, so it carries all four data bytes.
        QVERIFY(!OpenPods::Reassert::reassertFor(0x1A, QByteArray::fromHex("07"),
                                                 QByteArray::fromHex("07000000"))
                     .has_value());
    }

    void frameIsElevenBytesOfHeaderThenIdThenFourDataBytes()
    {
        const auto frame = OpenPods::Reassert::reassertFor(0x34, QByteArray::fromHex("0102030405"),
                                                           std::nullopt);
        QVERIFY(frame.has_value());
        QCOMPARE(frame->size(), controlFrameSize);
        QVERIFY(frame->startsWith(QByteArray::fromHex("040004000900")));
        QCOMPARE(static_cast<quint8>(frame->at(6)), quint8(0x34));
        // A fifth payload byte has nowhere to go on the wire.
        QCOMPARE(frame->mid(7), QByteArray::fromHex("01020304"));
        QCOMPARE(ControlCommand::parseActive(*frame).value_or(0), char(0x01));
    }

    void logLine_showsBothPayloadsAsTheFourWireBytes()
    {
        // The echo the daemon hands over is the four data bytes off an 11-byte frame.
        QCOMPARE(OpenPods::Reassert::reassertLogLine(0x1A, QByteArray::fromHex("0f000000"),
                                                     QByteArray::fromHex("07")),
                 QStringLiteral("Re-asserting 0x1A: pods report 0f000000, want 07000000"));
        QCOMPARE(OpenPods::Reassert::reassertLogLine(0x1A, std::nullopt, QByteArray::fromHex("07")),
                 QStringLiteral("Re-asserting 0x1A: pods report nothing, want 07000000"));
    }

private:
    void writeIni(const QByteArray &text)
    {
        QFile f(m_iniPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(f.write(text), qint64(text.size()));
    }

    QScopedPointer<QTemporaryDir> m_dir;
    QString m_iniPath;
};

QTEST_GUILESS_MAIN(TestSettingsReassert)
#include "tst_settingsreassert.moc"
