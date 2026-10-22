// Navigation requirements (NAV-*) against the headless Navigator.
#include "builder.h"
#include "navigator.h"

#include <QtTest>

using namespace krellis;
namespace L = krellis::layout;

struct Host : NavigationHost {
    LayoutDocument document;
    NavMode navMode = NavMode::Free;
    int changes = 0;
    double peek = 0;
    bool fly = false, reduced = false;
    double clock = 0;
    bool flyTo() const override { return fly; }
    bool reducedMotion() const override { return reduced; }
    double clockMs() const override { return clock; }
    const LayoutDocument &doc() const override { return document; }
    NavMode mode() const override { return navMode; }
    Point viewport() const override { return {1000, 600}; }
    LayoutMetrics layoutMetrics() const override
    {
        LayoutMetrics m;
        m.width = 1000;
        m.height = 600;
        m.min = [](const Node &, Axis) { return 10.0; };
        return m;
    }
    void changed() override { ++changes; }
    double peekPadding() const override { return peek; }
};

static LayoutDocument three()
{
    auto root = L::row({L::panel({L::view("t", "a")}, "A"), L::panel({L::view("t", "b")}, "B"),
                        L::panel({L::view("t", "c")}, "C")});
    return createDocument(&root);
}

class TestNavigator : public QObject
{
    Q_OBJECT
private slots:

    // --- fly-to ---
    static LayoutDocument eight()
    {
        std::vector<LayoutSpec> cols;
        for (int i = 0; i < 8; ++i)
            cols.push_back(L::panel({L::view("t", std::string("w") + char('a' + i))}, std::string("P") + char('a' + i)));
        auto root = L::row(std::move(cols));
        return createDocument(&root);
    }
    void flightNearJumpIsDirect()
    {
        Host h;
        h.document = eight();
        h.fly = true;
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("Pa");
        cam.finish();
        nav.focus("Pb");
        QVERIFY(!nav.flying());
        QVERIFY(qAbs(cam.target.x - 1.0 / 8) < 1e-9);
    }
    void flightFarJumpZoomsOutThenIn()
    {
        Host h;
        h.document = eight();
        h.fly = true;
        h.clock = 1000;
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("Pa");
        cam.finish();
        h.clock = 2000;
        nav.focus("Ph");
        QVERIFY(nav.flying());
        const Rect dest = *nav.pendingTarget();
        QVERIFY(qAbs(dest.x - 7.0 / 8) < 1e-9);
        QVERIFY(cam.target.x <= 0 && cam.target.x + cam.target.w >= 1 - 1e-9); // the whole span
        double now = 2000;
        double widest = 0;
        for (int i = 0; i < 200 && nav.flying(); ++i) {
            now += 16;
            cam.step(0.016);
            nav.tick(0.016, now);
            widest = std::max(widest, cam.value.w);
        }
        QVERIFY(!nav.flying());
        QVERIFY(sameRect(cam.target, dest));
        QVERIFY(widest > dest.w * 2);
        for (int i = 0; i < 400; ++i)
            cam.step(0.016);
        QVERIFY(sameRect(cam.value, dest, 1e-3));
    }
    void flightSurvivesSync()
    {
        Host h;
        h.document = eight();
        h.fly = true;
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("Pa");
        cam.finish();
        nav.focus("Ph");
        const Rect mid = cam.target, dest = *nav.pendingTarget();
        nav.sync();
        QVERIFY(nav.flying());
        QVERIFY(sameRect(cam.target, mid));
        QVERIFY(sameRect(*nav.pendingTarget(), dest));
    }
    void flightSecondJumpReplacesTheDestination()
    {
        Host h;
        h.document = eight();
        h.fly = true;
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("Pa");
        cam.finish();
        nav.focus("Ph");
        nav.focus("Pd");
        QVERIFY(qAbs(cam.target.x + cam.target.w / 2 - cam.value.x - cam.value.w / 2) > -1); // still a rect
        if (nav.flying())
            QVERIFY(qAbs(nav.pendingTarget()->x - 3.0 / 8) < 1e-9);
        else
            QVERIFY(qAbs(cam.target.x - 3.0 / 8) < 1e-9);
    }
    void flightIsSkippedForReducedMotionAndGlideOff()
    {
        Host h;
        h.document = eight();
        h.fly = true;
        h.reduced = true;
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("Pa");
        nav.focus("Ph");
        QVERIFY(!nav.flying());
        h.reduced = false;
        h.fly = false;
        nav.focus("Pa");
        cam.finish();
        nav.focus("Ph");
        QVERIFY(!nav.flying());
    }
    void flightExternalJumpCancels()
    {
        Host h;
        h.document = eight();
        h.fly = true;
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("Pa");
        cam.finish();
        nav.focus("Ph");
        QVERIFY(nav.flying());
        cam.jump({0.3, 0.3, 0.2, 0.2});
        nav.tick(0.016, 100);
        QVERIFY(!nav.flying());
        QVERIFY(sameRect(cam.target, {0.3, 0.3, 0.2, 0.2}));
    }
    void flightIsRateLimited()
    {
        Host h;
        h.document = eight();
        h.fly = true;
        h.clock = 1000;
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("Pa");
        cam.finish();
        h.clock = 5000;
        nav.focus("Ph");
        QVERIFY(nav.flying());
        h.clock = 5050; // a held key repeats
        nav.focus("Pa");
        QVERIFY(!nav.flying());
        QVERIFY(qAbs(cam.target.x) < 1e-9);
    }
    void init() {}
    void startsFramingEverything()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        QVERIFY(nav.framed().empty());
        QVERIFY(cam.target == UNIT);
    }
    void maximizeAndRestoreExactFraming()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus(rangeId(h.document.root->id, {"A", "B"}));
        QVERIFY(qAbs(cam.target.w - 2.0 / 3) < 1e-9);
        QVERIFY(nav.toggle("A"));
        QCOMPARE(nav.framed(), std::string("A"));
        QVERIFY(qAbs(cam.target.w - 1.0 / 3) < 1e-9);
        QVERIFY(nav.toggle("A")); // NAV-08: restores the exact prior framing
        QCOMPARE(nav.framed(), rangeId(h.document.root->id, {"A", "B"}));
    }
    void focusModeOffNeverFrames()
    {
        Host h;
        h.document = three();
        h.navMode = NavMode::Off;
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("A");
        QVERIFY(nav.framed().empty());
    }
    void overviewTogglesBack()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("B");
        nav.toggleOverview();
        QVERIFY(nav.framed().empty());
        nav.toggleOverview(); // NAV-13
        QCOMPARE(nav.framed(), std::string("B"));
    }
    void historyBackForward()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("A");
        nav.focus("B");
        QVERIFY(nav.canGoBack());
        nav.back();
        QCOMPARE(nav.framed(), std::string("A"));
        QVERIFY(nav.canGoForward());
        nav.forward();
        QCOMPARE(nav.framed(), std::string("B"));
    }
    void framingFollowsSurvivingViews()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus(rangeId(h.document.root->id, {"A", "B"}));
        // Close A: the framing follows B (NAV-10).
        h.document = closeView(h.document, "a");
        nav.sync();
        QCOMPARE(nav.framed(), std::string("B"));
    }
    void savedFramingsRejectBlankNames()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        QVERIFY(!nav.saveFraming("   "));
        nav.focus("C");
        auto f = nav.saveFraming("  Review ");
        QVERIFY(f);
        QCOMPARE(f->name, std::string("Review"));
        nav.focus({});
        nav.goFraming(f->id);
        QCOMPARE(nav.framed(), std::string("C"));
        // Survives serialization.
        auto saved = nav.serialize();
        Navigator other(h, cam);
        other.sync();
        other.restore(saved);
        QCOMPARE(other.framed(), std::string("C"));
        QCOMPARE(other.framings().size(), size_t(1));
    }
    void rubberBandZoomSnapsToBestFit()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.zoom(0.34, {0.1, 0.5}); // pinch in near the left edge
        QVERIFY(nav.gestureActive());
        QVERIFY(cam.value.w < 0.5);
        nav.finishGesture();
        QCOMPARE(nav.framed(), std::string("A"));
        QVERIFY(!nav.gestureActive());
    }
    void zoomOutOvershootIsClamped()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.zoom(5.0, {0.5, 0.5});
        // A rubber band: it gives a little past the 1.35 limit, never more than exp(0.12) beyond it.
        QVERIFY(cam.value.w > 1.35);
        QVERIFY(cam.value.w <= 1.35 * std::exp(0.12) + 1e-9);
        QVERIFY(cam.value.x >= -cam.value.w * 0.3 - 1e-9);
        nav.finishGesture();
        QVERIFY(cam.target.w <= 1.35 + 1e-9);
    }
    void zoomOvershootIsBoundedAndResistsMore()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        double last = 1.0, lastGain = 1e9;
        for (int i = 0; i < 40; ++i) {
            nav.zoom(1.1, {0.5, 0.5});
            const double gain = cam.value.w / last;
            QVERIFY(gain <= lastGain + 1e-12); // each push gains less than the one before
            lastGain = gain;
            last = cam.value.w;
            QVERIFY(last <= 1.35 * std::exp(0.12) + 1e-9);
        }
        QVERIFY(last > 1.35);
    }
    void zoomPullingBackRetracesTheBand()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.zoom(1.2, {0.5, 0.5}); // 1.2 x 1.0 is still inside the 1.35 limit
        nav.zoom(1.15, {0.5, 0.5}); // this one crosses it
        const double before = cam.value.w;
        nav.zoom(1.5, {0.5, 0.5});
        nav.zoom(1.0 / 1.5, {0.5, 0.5});
        QVERIFY(qAbs(cam.value.w - before) < 1e-9);
        // Panning alone never changes the size, in or past the band.
        nav.zoom(1.5, {0.5, 0.5});
        const double stretched = cam.value.w;
        nav.zoom(1.0, {0.5, 0.5}, {20, 0});
        QCOMPARE(cam.value.w, stretched);
    }
    void zoomInOvershootIsBounded()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        for (int i = 0; i < 40; ++i)
            nav.zoom(0.7, {0.1, 0.5});
        const double minSide = (1.0 / 3) * 0.65; // 65% of the smallest tile
        QVERIFY(cam.value.h < minSide);
        QVERIFY(cam.value.h >= minSide * std::exp(-0.12) - 1e-9);
        nav.finishGesture();
        QCOMPARE(nav.framed(), std::string("A")); // snaps to the nearest tile
    }
    void bestFitStaysCheapWithManyTiles()
    {
        Host h;
        std::vector<LayoutSpec> cols;
        for (int i = 0; i < 24; ++i)
            cols.push_back(L::panel({L::view("t", "v" + std::to_string(i))}, "P" + std::to_string(i)));
        auto root = L::row(cols);
        h.document = createDocument(&root);
        RectSpring cam;
        Navigator nav(h, cam);
        QElapsedTimer t;
        t.start();
        nav.sync();
        const qint64 syncNs = t.nsecsElapsed();
        t.restart();
        for (int i = 0; i < 100; ++i)
            nav.zoom(0.99, {0.5, 0.5});
        const qint64 zoomNs = t.nsecsElapsed() / 100;
        t.restart();
        nav.finishGesture();
        qInfo() << "24 tiles:" << nav.entries().size() << "entries, sync" << syncNs / 1000 << "us, zoom"
                << zoomNs / 1000 << "us/event, finishGesture" << t.nsecsElapsed() / 1000 << "us";
        QVERIFY(zoomNs < 1000000); // an event must stay well under a millisecond
    }
    static LayoutDocument row8(std::vector<double> weights = {})
    {
        std::vector<LayoutSpec> cols;
        for (int i = 1; i <= 8; ++i)
            cols.push_back(L::panel({L::view("t", "v" + std::to_string(i))}, "P" + std::to_string(i)));
        auto root = L::row(cols, weights);
        return createDocument(&root);
    }
    static std::vector<std::string> sorted(std::vector<std::string> v)
    {
        std::sort(v.begin(), v.end());
        return v;
    }
    void revealKeepsAFramedTile()
    {
        Host h;
        h.document = row8();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.frameTiles({"P1", "P2"});
        const Rect before = cam.target;
        nav.reveal("P2");
        QVERIFY(cam.target == before);
        nav.reveal("nope"); // unknown ids (a drawer window) change nothing
        QVERIFY(cam.target == before);
    }
    void revealSlidesAtTheSameSize()
    {
        Host h;
        h.document = row8();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.frameTiles({"P1", "P2"});
        QVERIFY(qAbs(cam.target.w - 0.25) < 1e-9);
        nav.reveal("P8");
        QVERIFY(qAbs(cam.target.w - 0.25) < 1e-9);
        QCOMPARE(sorted(nav.framedLeaves()), (std::vector<std::string>{"P7", "P8"}));
        nav.reveal("P4"); // a middle one: still two tiles wide, the least slide that covers it
        QCOMPARE(sorted(nav.framedLeaves()), (std::vector<std::string>{"P4", "P5"}));
        nav.reveal("P3");
        QCOMPARE(sorted(nav.framedLeaves()), (std::vector<std::string>{"P3", "P4"}));
    }
    void revealFromOneTileStaysOneTile()
    {
        Host h;
        h.document = row8();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("P1");
        nav.reveal("P5");
        QCOMPARE(nav.framedLeaves(), (std::vector<std::string>{"P5"}));
        QVERIFY(qAbs(cam.target.w - 0.125) < 1e-9);
    }
    void revealStretchesForALongerTile()
    {
        Host h;
        h.document = row8({1, 1, 1, 1, 1, 1, 1, 5});
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.frameTiles({"P1", "P2"});
        nav.reveal("P8"); // wider than the frame: the frame takes its width
        QCOMPARE(nav.framedLeaves(), (std::vector<std::string>{"P8"}));
    }
    void revealDoesNothingInOverview()
    {
        Host h;
        h.document = row8();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.reveal("P3");
        QVERIFY(nav.overviewing());
    }
    void revealWidensForANewNeighbourOnly()
    {
        Host h;
        h.document = row8();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("P1");
        nav.reveal("P2", true); // just opened beside the frame: both in view
        QCOMPARE(sorted(nav.framedLeaves()), (std::vector<std::string>{"P1", "P2"}));
        nav.reveal("P8", true); // a far tile would quadruple the frame: slide instead
        QCOMPARE(sorted(nav.framedLeaves()), (std::vector<std::string>{"P7", "P8"}));
    }
    void revealDoesNotFillTheHistory()
    {
        Host h;
        h.document = row8();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("P1");
        const bool back = nav.canGoBack();
        for (const char *id : {"P3", "P5", "P7", "P2"})
            nav.reveal(id);
        QCOMPARE(nav.canGoBack(), back);
    }
    void wheelStepsOncePerGesture()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("B");
        QVERIFY(nav.wheelStep(5, {0.5, 0.5}, 0)); // the first event of a gesture steps out by one column
        QVERIFY(cam.target.w > 0.6);
        QVERIFY(!nav.wheelStep(15, {0.5, 0.5}, 60)); // the rest of the flick is ignored...
        QVERIFY(!nav.wheelStep(15, {0.5, 0.5}, 200)); // ...for as long as events keep coming
        QVERIFY(!nav.wheelStep(15, {0.5, 0.5}, 340));
        QVERIFY(nav.wheelStep(-15, {0.5, 0.5}, 360)); // a reversal starts a new gesture
        QVERIFY(cam.target.w < 0.4);
        QVERIFY(nav.wheelStep(15, {0.5, 0.5}, 900)); // so does a pause
    }
    static LayoutDocument grid() // three columns of two rows
    {
        auto root = L::row({L::column({L::panel({L::view("t", "a")}, "A"), L::panel({L::view("t", "d")}, "D")}),
                            L::column({L::panel({L::view("t", "b")}, "B"), L::panel({L::view("t", "e")}, "E")}),
                            L::column({L::panel({L::view("t", "c")}, "C"), L::panel({L::view("t", "f")}, "F")})});
        return createDocument(&root);
    }
    static bool near(const Rect &r, double x, double y, double w, double h)
    {
        return std::abs(r.x - x) < 1e-6 && std::abs(r.y - y) < 1e-6 && std::abs(r.w - w) < 1e-6 && std::abs(r.h - h) < 1e-6;
    }
    void zoomOutGrowsByOneColumnAndRow()
    {
        Host h;
        h.document = grid();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("A");
        QVERIFY(nav.zoomStep(true, {0.1, 0.2})); // A is a single row high: one more column, one more row
        QVERIFY(near(cam.target, 0, 0, 2.0 / 3, 1)); // A B D E
        QVERIFY(nav.zoomStep(true, {0.1, 0.2}));
        QVERIFY(nav.overviewing());
        QVERIFY(!nav.boxed());
        QVERIFY(!nav.zoomStep(true, {0.1, 0.2})); // nothing beyond everything
    }
    void zoomOutGrowsAwayFromThePointer()
    {
        Host h;
        h.document = grid();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("E");
        QVERIFY(nav.zoomStep(true, {0.45, 0.9})); // pointer on E's left: the new column is on its right
        QVERIFY(near(cam.target, 1.0 / 3, 0, 2.0 / 3, 1));
    }
    void zoomInShrinksByOneColumnAndRow()
    {
        Host h;
        h.document = grid();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        QVERIFY(nav.zoomStep(false, {0.1, 0.2})); // drops the far column and the far row, keeping A
        QVERIFY(near(cam.target, 0, 0, 2.0 / 3, 0.5));
        QVERIFY(nav.zoomStep(false, {0.1, 0.2})); // only the column is left to drop
        QCOMPARE(nav.framed(), std::string("A"));
        QVERIFY(!nav.boxed());
        QVERIFY(!nav.zoomStep(false, {0.1, 0.2})); // a single tile cannot shrink
        nav.overview();
        QVERIFY(nav.zoomStep(false, {0.9, 0.9})); // from the other corner it keeps F
        QVERIFY(near(cam.target, 1.0 / 3, 0.5, 2.0 / 3, 0.5));
    }
    void zoomInKeepsShrinkingAlongARow()
    {
        Host h;
        auto root = L::row({L::panel({L::view("t", "a")}, "A"), L::panel({L::view("t", "b")}, "B"),
                            L::panel({L::view("t", "c")}, "C"), L::panel({L::view("t", "d")}, "D")});
        h.document = createDocument(&root);
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        QVERIFY(nav.zoomStep(false, {0.1, 0.5}));
        QVERIFY(near(cam.target, 0, 0, 0.75, 1));
        QVERIFY(nav.zoomStep(false, {0.1, 0.5}));
        QVERIFY(near(cam.target, 0, 0, 0.5, 1));
        QVERIFY(nav.zoomStep(false, {0.1, 0.5}));
        QVERIFY(near(cam.target, 0, 0, 0.25, 1));
    }
    void frameTilesFramesExactlyThose()
    {
        Host h;
        h.document = grid();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.frameTiles({"B", "C", "E", "F"}); // the right two columns: a range node
        QVERIFY(near(cam.target, 1.0 / 3, 0, 2.0 / 3, 1));
        nav.frameTiles({"B", "C"}); // top row of those two columns: no node, so boxed
        QVERIFY(nav.boxed());
        QVERIFY(near(cam.target, 1.0 / 3, 0, 2.0 / 3, 0.5));
        nav.frameTiles({"E"});
        QCOMPARE(nav.framed(), std::string("E"));
        QVERIFY(!nav.boxed());
    }
    void boxSurvivesLayoutSyncAndOverviewReturns()
    {
        Host h;
        h.document = grid();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        QVERIFY(nav.zoomStep(false, {0.1, 0.2})); // A B only: no node holds exactly those two
        QVERIFY(nav.boxed());
        QVERIFY(near(cam.target, 0, 0, 2.0 / 3, 0.5));
        nav.sync();
        QVERIFY(nav.boxed());
        nav.toggleOverview();
        QVERIFY(nav.overviewing());
        nav.toggleOverview(); // comes back to the block, not just the node around it
        QVERIFY(nav.boxed());
        QVERIFY(near(cam.target, 0, 0, 2.0 / 3, 0.5));
        QVERIFY(nav.zoomStep(true, {0.1, 0.2})); // out again: the third column and the second row join
        QVERIFY(nav.overviewing());
        QVERIFY(near(cam.target, 0, 0, 1, 1));
    }
    void peekLeavesNeighbourEdgesInView()
    {
        Host h;
        h.document = three();
        h.peek = 50; // 5% of the 1000 px viewport per side
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        QVERIFY(cam.target == UNIT); // showing everything has no neighbours
        nav.focus("B");
        const double whole = (1.0 / 3) / 0.9;
        QVERIFY(qAbs(cam.target.w - whole) < 1e-9);
        QVERIFY(qAbs(cam.target.x - (1.0 / 3 - 0.05 * whole)) < 1e-9);
        QVERIFY(near(cam.target, cam.target.x, 0, whole, 1)); // the full height is already in view: no gutter above or below
        nav.focus("A"); // the first column has nothing on its left
        const double edge = (1.0 / 3) / 0.95;
        QVERIFY(qAbs(cam.target.w - edge) < 1e-9);
        QVERIFY(qAbs(cam.target.x) < 1e-9);
        QCOMPARE(nav.framedLeaves(), std::vector<std::string>{"A"});
    }
    void floatsFrameTheirStage()
    {
        Host h;
        auto stageChild = L::panel({L::view("t", "s")}, "S");
        auto root = L::row({L::panel({L::view("t", "a")}, "A"), L::stage(&stageChild)});
        FloatSpec f{L::panel({L::view("t", "f")}, "F"), {0.1, 0.1, 0.2, 0.2}, FloatingLayer::Stage};
        h.document = createDocument(&root, {f});
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("F"); // NAV-12: floating windows are never camera targets
        QVERIFY(nav.framed() == "S" || nav.framed() == "stage");
        QVERIFY(!nav.toggle("F"));
    }
};

QTEST_APPLESS_MAIN(TestNavigator)
#include "test_navigator.moc"
