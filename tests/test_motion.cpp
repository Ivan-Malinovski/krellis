#include "motion.h"

#include <QtTest>

using namespace krellis;

class TestMotion : public QObject
{
    Q_OBJECT
private slots:
    void springSettles()
    {
        RectSpring s({0, 0, 1, 1});
        s.target = {0.5, 0.25, 0.5, 0.5};
        int frames = 0;
        while (s.step(1.0 / 60) && frames < 600)
            ++frames;
        QVERIFY(frames < 120); // settles in well under two seconds
        QVERIFY(sameRectEps(s.value, s.target));
        QVERIFY(!s.moving());
    }
    void springIsDeterministic()
    {
        RectSpring a({0, 0, 1, 1}), b({0, 0, 1, 1});
        a.target = b.target = {0.2, 0.3, 0.4, 0.5};
        for (int i = 0; i < 30; ++i) {
            a.step(1.0 / 144);
            b.step(1.0 / 144);
        }
        QVERIFY(a.value == b.value);
    }
    void tweenInterpolates()
    {
        LayoutTween t;
        t.begin({{"a", {0, 0, 100, 100}}}, 0);
        QVERIFY(t.active());
        Rect mid = t.apply("a", {100, 0, 100, 100});
        QCOMPARE(mid.x, 0.0);
        t.step(MotionLayoutMs());
        QVERIFY(!t.active());
        QCOMPARE(t.apply("a", {100, 0, 100, 100}).x, 100.0);
        QCOMPARE(t.apply("missing", {7, 0, 1, 1}).x, 7.0);
    }
    void interruptedTweenKeepsVelocity()
    {
        LayoutTween t;
        const Rect target{100, 0, 100, 100};
        t.begin({{"a", {0, 0, 100, 100}}}, 0);
        t.step(99);
        const Rect previous = t.apply("a", target);
        t.step(100);
        const Rect captured = t.apply("a", target);
        const double speed = captured.x - previous.x;
        t.begin({{"a", captured}}, 100);
        const Rect reverse{-100, 0, 100, 100};
        QCOMPARE(t.apply("a", reverse).x, captured.x);
        t.step(101);
        const double after = t.apply("a", reverse).x - captured.x;
        QVERIFY(speed > 0);
        QVERIFY(after > 0); // reverses smoothly, not an instant velocity flip
        QVERIFY(std::abs(after - speed) < 0.02);
        t.step(560);
        QCOMPARE(t.apply("a", reverse), reverse);
    }
    void shortAndInstantTweens()
    {
        LayoutTween t;
        const Rect target{10, 20, 30, 40};
        t.begin({{"a", {0, 0, 100, 100}}}, 100, 240);
        t.step(339);
        QVERIFY(t.active());
        t.step(340);
        QVERIFY(!t.active());
        QCOMPARE(t.apply("a", target), target);
        t.begin({{"a", target}}, 400, 0);
        QVERIFY(!t.active());
        QCOMPARE(t.apply("a", target), target);
    }
    void dockEaseShape()
    {
        QCOMPARE(DOCK_EASE(0), 0.0);
        QCOMPARE(DOCK_EASE(1), 1.0);
        QVERIFY(DOCK_EASE(0.5) > 0.75); // fast start, long settle
        QVERIFY(DOCK_EASE(0.2) < DOCK_EASE(0.4));
    }
    void hyprlandCurvesHaveIndependentClocks()
    {
        // Evaluate the two parametric cubics at t=.5: known x/y pairs.
        QVERIFY(std::abs(HyprlandMotion::ease(.33125) - .875) < .0001);
        QVERIFY(std::abs(HyprlandMotion::fade(.59375) - .6875) < .0001);
        QCOMPARE(HyprlandMotion::ease(-1), 0.0);
        QCOMPARE(HyprlandMotion::fade(2), 1.0);
        // Opening is fully opaque while its geometry is still settling.
        QCOMPARE(HyprlandMotion::fade(200 / HyprlandMotion::fadeInMs), 1.0);
        QVERIFY(HyprlandMotion::ease(200 / HyprlandMotion::inMs) < 1);
        // The named Hyprland curve differs from the polynomial used by Trellis.
        QVERIFY(std::abs(HyprlandMotion::ease(.25) - easeOutQuint(.25)) > .01);
    }
    void hyprlandWindowStyles()
    {
        const Rect output{1280, -300, 1280, 800};
        const Rect window{1400, -200, 400, 200};
        QCOMPARE(HyprlandMotion::collapsed(window, output, "popin"), (Rect{1426, -187, 348, 174}));
        QCOMPARE(HyprlandMotion::collapsed(window, output, "gnomed"), (Rect{1400, -100, 400, 0}));
        QCOMPARE(HyprlandMotion::collapsed(window, output, "slide"), (Rect{1400, -500, 400, 200}));
        QCOMPARE(HyprlandMotion::collapsed({1300, 0, 100, 100}, output, "slide"), (Rect{1200, 0, 100, 100}));
        QCOMPARE(HyprlandMotion::collapsed({2450, 0, 100, 100}, output, "slide"), (Rect{2550, 0, 100, 100}));
        QCOMPARE(HyprlandMotion::collapsed({1800, 380, 100, 100}, output, "slide"), (Rect{1800, 500, 100, 100}));
        // Small cards never grow when collapsed.
        QCOMPARE(HyprlandMotion::collapsed({0, 0, 3, 4}, output, "popin"), (Rect{0, 0, 3, 4}));
    }

private:
    static double MotionLayoutMs() { return Motion::layoutMs; }
};

QTEST_APPLESS_MAIN(TestMotion)
#include "test_motion.moc"
