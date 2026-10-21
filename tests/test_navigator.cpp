// Navigation requirements (NAV-*) against the headless Navigator.
#include "builder.h"
#include "navigator.h"

#include <QtTest>

using namespace trellis;
namespace L = trellis::layout;

struct Host : NavigationHost {
    LayoutDocument document;
    NavMode navMode = NavMode::Free;
    int changes = 0;
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
        QVERIFY(cam.value.w <= 1.35 + 1e-9);
        QVERIFY(cam.value.x >= -cam.value.w * 0.3 - 1e-9);
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
