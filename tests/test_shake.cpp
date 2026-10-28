#include <QtTest>

#include "../core/shake.h"

using krellis::ShakeDetector;

class TestShake : public QObject
{
    Q_OBJECT
private slots:
    void aQuickBackAndForthFires()
    {
        ShakeDetector d;
        bool fired = false;
        std::int64_t t = 0;
        // 100 px swings every 80 ms: right, left, right, left, right = four turns inside 400 ms.
        for (double x : {0., 100., 0., 100., 0., 100.}) {
            fired = d.feed(x, 300, t);
            t += 80;
        }
        QVERIFY(fired);
    }
    void aStraightDragNeverFires()
    {
        ShakeDetector d;
        for (int i = 0; i < 200; ++i)
            QVERIFY(!d.feed(i * 10., 300 + i, i * 8));
    }
    void aSlowWiggleDoesNot()
    {
        ShakeDetector d;
        bool fired = false;
        std::int64_t t = 0;
        for (double x : {0., 100., 0., 100., 0., 100.}) {
            fired |= d.feed(x, 300, t);
            t += 400; // each turn is 400 ms after the last: never four inside 600 ms
        }
        QVERIFY(!fired);
    }
    void smallJitterDoesNot()
    {
        ShakeDetector d;
        bool fired = false;
        for (int i = 0; i < 100; ++i)
            fired |= d.feed(i % 2 ? 10 : 0, 300, i * 10);
        QVERIFY(!fired);
    }
    void verticalShakesCount()
    {
        ShakeDetector d;
        bool fired = false;
        std::int64_t t = 0;
        for (double y : {0., 120., 0., 120., 0., 120.}) {
            fired |= d.feed(500, y, t);
            t += 70;
        }
        QVERIFY(fired);
    }
    void firesOnceAndRearms()
    {
        ShakeDetector d;
        int fires = 0;
        std::int64_t t = 0;
        for (int round = 0; round < 2; ++round)
            for (double x : {0., 100., 0., 100., 0., 100.}) {
                fires += d.feed(x, 0, t);
                t += 80;
            }
        QCOMPARE(fires, 2);
    }
};

QTEST_APPLESS_MAIN(TestShake)
#include "test_shake.moc"
