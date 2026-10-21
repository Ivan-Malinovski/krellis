#include "chord_hint.h"
#include "safemode.h"

#include <QtTest>

using namespace trellis;

class TestChordSafemode : public QObject
{
    Q_OBJECT
private slots:
    void holdShowsAfterTheDelay()
    {
        ChordHint h(400);
        h.setModifiers(true, false, false, 0);
        QVERIFY(!h.pending());
        h.setModifiers(true, true, false, 100);
        QVERIFY(h.pending());
        QCOMPARE(h.deadline(), qint64(500));
        QVERIFY(!h.tick(499));
        QVERIFY(h.tick(500));
        QVERIFY(h.shown());
        QVERIFY(!h.tick(900)); // reported once
    }
    void releaseHidesAndRestarts()
    {
        ChordHint h(400);
        h.setModifiers(true, true, false, 0);
        h.tick(400);
        QVERIFY(h.shown());
        h.setModifiers(true, false, false, 450);
        QVERIFY(!h.shown() && !h.pending());
        h.setModifiers(true, true, false, 600); // a second hold starts over
        QVERIFY(h.pending());
        QCOMPARE(h.deadline(), qint64(1000));
    }
    void anotherModifierCancels()
    {
        ChordHint h(400);
        h.setModifiers(true, true, false, 0);
        h.setModifiers(true, true, true, 50); // Shift joined: a different chord
        QVERIFY(!h.pending());
        QVERIFY(!h.tick(1000));
    }
    void otherInputDismissesUntilReleased()
    {
        ChordHint h(400);
        h.setModifiers(true, true, false, 0);
        h.otherInput(); // Meta+Alt+Up fired quickly
        QVERIFY(!h.pending());
        h.setModifiers(true, true, false, 10); // repeated modifier events must not re-arm it
        QVERIFY(!h.pending());
        QVERIFY(!h.tick(2000));
        h.setModifiers(false, false, false, 2100);
        h.setModifiers(true, true, false, 2200);
        QVERIFY(h.pending());
    }
    void dismissWhileShown()
    {
        ChordHint h(400);
        h.setModifiers(true, true, false, 0);
        h.tick(400);
        h.dismiss();
        QVERIFY(!h.shown());
        QVERIFY(!h.tick(5000));
    }

    void cleanStartLoads()
    {
        QVERIFY(decideBoot({}, 1000).allow);
        QCOMPARE(decideBoot({}, 1000).strikes, 0);
        BootMarker clean{500, true, false, 3};
        QCOMPARE(decideBoot(clean, 1000).strikes, 0);
    }
    void oneEarlyDeathStillLoads()
    {
        BootMarker m{1000, false, false, 0};
        const auto d = decideBoot(m, 20'000);
        QVERIFY(d.allow);
        QCOMPARE(d.strikes, 1);
    }
    void twoEarlyDeathsInARowBlock()
    {
        BootMarker m{1000, false, false, 1};
        const auto d = decideBoot(m, 20'000);
        QVERIFY(!d.allow);
        QCOMPARE(d.strikes, 2);
    }
    void aLongSessionIsNotACrashLoop()
    {
        BootMarker m{1000, false, true, 1}; // survived the first two minutes, then KWin died
        QVERIFY(decideBoot(m, 5'000'000).allow);
        QCOMPARE(decideBoot(m, 5'000'000).strikes, 0);
    }
    void aBlockedLoopGetsOneRetryLater()
    {
        BootMarker m{1000, false, false, 2};
        QVERIFY(decideBoot(m, 1000 + BOOT_FRESH_MS + 1).allow);
        QCOMPARE(decideBoot(m, 1000 + BOOT_FRESH_MS + 1).strikes, 1);
    }
    void clockGoingBackwardsIsNotTheSameBurst()
    {
        BootMarker m{50'000, false, false, 1};
        QVERIFY(decideBoot(m, 10'000).allow);
    }
};

QTEST_APPLESS_MAIN(TestChordSafemode)
#include "test_chord_safemode.moc"
