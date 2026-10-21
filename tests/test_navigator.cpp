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
    void wheelStepsAfterThreshold()
    {
        Host h;
        h.document = three();
        RectSpring cam;
        Navigator nav(h, cam);
        nav.sync();
        nav.focus("B");
        QVERIFY(!nav.wheelStep(5, {0.5, 0.5}, 0)); // below the 12px threshold
        QVERIFY(nav.wheelStep(10, {0.5, 0.5}, 20)); // accumulates, steps out
        QVERIFY(nav.framed().empty());
        QVERIFY(!nav.wheelStep(20, {0.5, 0.5}, 60)); // inside the 140ms cooldown
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
