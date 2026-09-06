// The cycle this is meant to replace, cycleNoiseControlMode in main.cpp, still does (current + 1) % 4 and so sends Off to a Pro 3 and Adaptive to an H1 model.

#include "modecycle.hpp"

#include <QtTest/QtTest>

class TestModeCycle : public QObject
{
    Q_OBJECT

    static constexpr int off = 0;
    static constexpr int noiseCancellation = 1;
    static constexpr int transparency = 2;
    static constexpr int adaptive = 3;
    static constexpr int unknown = -1;

private slots:
    void fullTable_cyclesAllFourInAppleOrder()
    {
        QCOMPARE(OpenPods::availableModes(true, true),
                 (QList<int>{off, noiseCancellation, transparency, adaptive}));
        QCOMPARE(OpenPods::nextNoiseMode(off, true, true), noiseCancellation);
        QCOMPARE(OpenPods::nextNoiseMode(noiseCancellation, true, true), transparency);
        QCOMPARE(OpenPods::nextNoiseMode(transparency, true, true), adaptive);
        QCOMPARE(OpenPods::nextNoiseMode(adaptive, true, true), off);
    }

    void pro3_noOff_neverYieldsOffAndWrapsToAnc()
    {
        QCOMPARE(OpenPods::availableModes(false, true),
                 (QList<int>{noiseCancellation, transparency, adaptive}));
        QCOMPARE(OpenPods::nextNoiseMode(noiseCancellation, false, true), transparency);
        QCOMPARE(OpenPods::nextNoiseMode(transparency, false, true), adaptive);
        QCOMPARE(OpenPods::nextNoiseMode(adaptive, false, true), noiseCancellation);
        for (int current = unknown; current <= adaptive; ++current) {
            QVERIFY2(OpenPods::nextNoiseMode(current, false, true) != off,
                     qPrintable(QStringLiteral("Off offered to a Pro 3 from mode %1").arg(current)));
        }
    }

    void h1_noAdaptive_neverYieldsAdaptiveAndWrapsToOff()
    {
        QCOMPARE(OpenPods::availableModes(true, false),
                 (QList<int>{off, noiseCancellation, transparency}));
        QCOMPARE(OpenPods::nextNoiseMode(off, true, false), noiseCancellation);
        QCOMPARE(OpenPods::nextNoiseMode(noiseCancellation, true, false), transparency);
        QCOMPARE(OpenPods::nextNoiseMode(transparency, true, false), off);
        for (int current = unknown; current <= adaptive; ++current) {
            QVERIFY2(OpenPods::nextNoiseMode(current, true, false) != adaptive,
                     qPrintable(QStringLiteral("Adaptive offered to an H1 model from mode %1").arg(current)));
        }
    }

    void unknownCurrent_startsAtHead()
    {
        QCOMPARE(OpenPods::nextNoiseMode(unknown, true, true), off);
        QCOMPARE(OpenPods::nextNoiseMode(adaptive + 1, true, true), off);
        QCOMPARE(OpenPods::nextNoiseMode(unknown, false, true), noiseCancellation);
        QCOMPARE(OpenPods::nextNoiseMode(adaptive + 1, false, true), noiseCancellation);
    }

    void currentTheModelLacks_startsAtHead()
    {
        // Head and walk-forward coincide for every lacked mode, so this pins the returned value, not the choice between them.
        QCOMPARE(OpenPods::nextNoiseMode(off, false, true), noiseCancellation);
        QCOMPARE(OpenPods::nextNoiseMode(adaptive, true, false), off);
    }

    void bothFlagsOff_cyclesAncAndTransparencyOnly()
    {
        QCOMPARE(OpenPods::availableModes(false, false),
                 (QList<int>{noiseCancellation, transparency}));
        QCOMPARE(OpenPods::nextNoiseMode(noiseCancellation, false, false), transparency);
        QCOMPARE(OpenPods::nextNoiseMode(transparency, false, false), noiseCancellation);
        QCOMPARE(OpenPods::nextNoiseMode(off, false, false), noiseCancellation);
        QCOMPARE(OpenPods::nextNoiseMode(adaptive, false, false), noiseCancellation);
        QCOMPARE(OpenPods::nextNoiseMode(unknown, false, false), noiseCancellation);
    }
};

QTEST_GUILESS_MAIN(TestModeCycle)
#include "tst_modecycle.moc"
