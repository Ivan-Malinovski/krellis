// Behavioral cases derived from the reference model/navigation/resize contracts.
// Fixtures and assertions are written independently for the C++ model.
#include "document.h"
#include "navigator.h"
#include "resize.h"

#include <QtTest>
#include <cmath>

using namespace trellis;

namespace {
using Strings = std::vector<std::string>;

NodePtr tile(const std::string &id) { return makePanel(id, {id + "-view"}); }

LayoutDocument document(NodePtr root)
{
    LayoutDocument doc;
    doc.root = std::move(root);
    for (const auto &panel : panelsOf(doc.root))
        for (const auto &view : panel->views)
            doc.views[view] = {"app", {}, {}};
    return doc;
}

LayoutMetrics metrics()
{
    return {1200, 800, [](const Node &, Axis axis) { return axis == Axis::X ? 120.0 : 80.0; }};
}

struct NavHost : NavigationHost {
    LayoutDocument state = document(makeSplit("row", Axis::X, {tile("mail"), tile("notes"), tile("web")}));
    std::vector<bool> gestureEvents;
    const LayoutDocument &doc() const override { return state; }
    NavMode mode() const override { return NavMode::Free; }
    Point viewport() const override { return {1200, 800}; }
    LayoutMetrics layoutMetrics() const override { return metrics(); }
    void gestureChanged(bool active) override { gestureEvents.push_back(active); }
};
}

class TestMoreSemantics : public QObject
{
    Q_OBJECT
private slots:
    void savedFramingUsesSurvivorsAndThenHasNoDestination()
    {
        NavHost host;
        RectSpring camera;
        Navigator nav(host, camera);
        nav.sync();
        nav.focus(rangeId("row", {"mail", "notes"}));
        const auto saved = nav.saveFraming("Correspondence");
        QVERIFY(saved);

        host.state = closeView(host.state, "mail-view");
        nav.sync();
        nav.focus("web");
        nav.goFraming(saved->id);
        QCOMPARE(nav.framed(), std::string("notes"));

        host.state = closeView(host.state, "notes-view");
        nav.sync();
        QVERIFY(savedFrameDestination(host.state.root, saved->frame).empty());
        // The bookmark keeps its original membership for future serialization.
        QCOMPARE(nav.framings().front().frame, (Strings{"mail", "notes"}));
        nav.goFraming(saved->id);
        QVERIFY(nav.framed().empty());
        QVERIFY(sameRect(camera.target, UNIT));
    }

    void historyOfDeletedPanelFallsBackToStageWithoutLosingForwardVisit()
    {
        NavHost host;
        host.state = document(makeSplit("workspace", Axis::X, {tile("mail"), makeStage("desk"), tile("web")}));
        RectSpring camera;
        Navigator nav(host, camera);
        nav.sync();
        nav.focus("mail");
        nav.focus("web");
        host.state = closeView(host.state, "mail-view");
        nav.sync();

        nav.back();
        QCOMPARE(nav.framed(), std::string("desk"));
        QVERIFY(sameRect(camera.target, nav.entries().at("desk").rect));
        QVERIFY(nav.canGoForward());
        nav.forward();
        QCOMPARE(nav.framed(), std::string("web"));
    }

    void cancellingGestureRestoresFrameAndKeepsForwardHistory()
    {
        NavHost host;
        RectSpring camera;
        Navigator nav(host, camera);
        nav.sync();
        nav.focus("mail");
        nav.focus("web");
        nav.back();
        camera.finish();
        const Rect before = camera.value;
        const Navigation saved = nav.serialize();

        nav.zoom(1.6, {0.7, 0.4}, {-110, 30});
        nav.zoom(0.9, {0.7, 0.4});
        QVERIFY(nav.gestureActive());
        QVERIFY(!sameRect(camera.value, before));
        // The runtime's cancelled gesture focuses the existing framing.
        nav.focus(nav.framed());
        QVERIFY(!nav.gestureActive());
        QVERIFY(sameRect(camera.target, before));
        QCOMPARE(nav.serialize().frame, saved.frame);
        QCOMPARE(host.gestureEvents, (std::vector<bool>{true, false}));
        nav.finishGesture(); // A later release must not commit the abandoned candidate.
        QCOMPARE(host.gestureEvents.size(), size_t(2));
        QVERIFY(nav.canGoForward());
        nav.forward();
        QCOMPARE(nav.framed(), std::string("web"));
    }

    void historyRecordsChangedMembershipEvenWhenGroupIdIsUnchanged()
    {
        const auto first = recordVisit({{"workspace", {}}}, 0, {"work", {"mail", "notes"}});
        const auto changed = recordVisit(first.visits, first.index, {"work", {"notes"}});
        QCOMPARE(changed.visits.size(), size_t(3));
        QCOMPARE(changed.index, 2);
        QCOMPARE(changed.visits[1].leaves, (Strings{"mail", "notes"}));
        QCOMPARE(changed.visits[2].leaves, (Strings{"notes"}));
        const auto repeated = recordVisit(changed.visits, changed.index, changed.visits.back());
        QCOMPARE(repeated.visits.size(), changed.visits.size());
        QCOMPARE(repeated.index, changed.index);
    }

    void missingHiddenDockNeighbourUsesFallbackLayer()
    {
        auto doc = document(makeSplit("row", Axis::X, {tile("mail"), tile("notes"), tile("web")}, {2, 3, 5}));
        const auto original = locatePanel(doc, "mail").panel;
        doc = hidePanel(doc, "mail");
        QCOMPARE(doc.hidden.size(), size_t(1));
        QCOMPARE(doc.hidden.front().restore.beside, std::string("notes"));
        doc = closeView(doc, "notes-view");
        doc = restorePanel(doc, "mail", FloatingLayer::Stage);
        QVERIFY(doc.hidden.empty());
        const auto location = locatePanel(doc, "mail");
        QVERIFY(location.where == PanelLocation::Where::Floating);
        QVERIFY(location.panel == original);
        QVERIFY(location.floatState.layer == FloatingLayer::Stage);
        QVERIFY(sameRect(location.floatState.rect, {0.25, 0.2, 0.5, 0.6}));
        QCOMPARE(leafIds(doc.root), (Strings{"web"}));
        QVERIFY(doc.views.count("mail-view"));
    }

    void restoringOnlyHiddenPanelRecreatesRoot()
    {
        auto doc = document(tile("notes"));
        const auto original = doc.root;
        doc = hidePanel(doc, "notes");
        doc = hidePanel(doc, "notes"); // Repeated minimize preserves the first target.
        QVERIFY(!doc.root);
        QCOMPARE(doc.hidden.size(), size_t(1));
        doc = restorePanel(doc, "notes", FloatingLayer::Overlay);
        QVERIFY(doc.root == original);
        QVERIFY(doc.hidden.empty());
        QVERIFY(doc.floating.empty());
    }

    void hiddenStageChildRestoresAsTabsIntoNewOccupant()
    {
        auto doc = document(makeSplit("row", Axis::X, {tile("mail"), makeStage("desk", tile("notes"))}));
        doc = hidePanel(doc, "notes");
        QVERIFY(doc.hidden.front().restore.kind == RestoreTarget::Kind::Tab);
        QCOMPARE(doc.hidden.front().restore.panel, std::string("desk"));
        QVERIFY(!findStage(doc.root)->child);
        doc.views["web-view"] = {"app", {}, {}};
        doc = insertPanel(doc, tile("web"), DockTarget::intoPanel("desk"));
        doc = restorePanel(doc, "notes", FloatingLayer::Overlay);
        QVERIFY(doc.hidden.empty());
        QVERIFY(doc.floating.empty());
        const auto occupant = findStage(doc.root)->child;
        QVERIFY(occupant);
        QCOMPARE(occupant->id, std::string("web"));
        QCOMPARE(occupant->views, (Strings{"web-view", "notes-view"}));
        QCOMPARE(occupant->selected, std::string("notes-view"));
        QVERIFY(!locatePanel(doc, "notes"));
    }

    void repairFiltersUnknownTypesAndRepairsSelection()
    {
        LayoutDocument broken;
        broken.root = makeStage("desk", makePanel("notes", {"plugin", "known", "missing", "known"}, "plugin"));
        broken.views = {{"plugin", {"uninstalled", {}, {}}}, {"known", {"editor", "Kept title", {{"key", "value"}}}},
                        {"orphan", {"editor", {}, {}}}};
        const auto repaired = sanitize(broken, [](const std::string &type) { return type == "editor"; });
        const auto panel = findStage(repaired.root)->child;
        QVERIFY(panel);
        QCOMPARE(panel->views, (Strings{"known"}));
        QCOMPARE(panel->selected, std::string("known"));
        QCOMPARE(repaired.views.size(), size_t(1));
        QCOMPARE(repaired.views.at("known").title, std::string("Kept title"));
        QCOMPARE(repaired.views.at("known").params.at("key"), std::string("value"));
        QCOMPARE(findStage(broken.root)->child->selected, std::string("plugin"));
    }

    void repairDeduplicatesAcrossAllPlacementsInTraversalOrder()
    {
        auto broken = document(tile("mail"));
        broken.views["spare"] = {"app", {}, {}};
        broken.views["floating"] = {"app", {}, {}};
        broken.views["hidden"] = {"app", {}, {}};
        broken.floating.push_back({makePanel("mail", {"spare"}), {0.1, 0.1, 0.2, 0.2}, 1, FloatingLayer::Overlay});
        broken.floating.push_back({makePanel("float", {"mail-view", "floating"}, "mail-view"),
                                   {0.1, 0.1, 0.2, 0.2}, 2, FloatingLayer::Overlay});
        broken.hidden.push_back({makePanel("hidden", {"floating", "hidden"}, "floating"), {}});
        const auto repaired = sanitize(broken);
        QCOMPARE(repaired.floating.size(), size_t(1));
        QCOMPARE(repaired.floating.front().panel->id, std::string("float"));
        QCOMPARE(repaired.floating.front().panel->views, (Strings{"floating"}));
        QCOMPARE(repaired.floating.front().panel->selected, std::string("floating"));
        QCOMPARE(repaired.hidden.size(), size_t(1));
        QCOMPARE(repaired.hidden.front().panel->views, (Strings{"hidden"}));
        QCOMPARE(repaired.hidden.front().panel->selected, std::string("hidden"));
        QCOMPARE(viewIds(repaired), (Strings{"mail-view", "floating", "hidden"}));
        QVERIFY(!repaired.views.count("spare"));
    }

    void nestedPushRollbackRestoresOriginalTreeOnBothAxes()
    {
        for (const Axis axis : {Axis::X, Axis::Y}) {
            const Axis other = axis == Axis::X ? Axis::Y : Axis::X;
            const auto inner = makeSplit("inside", axis, {tile("notes"), tile("web")}, {0.5, 0.5});
            const auto cross = makeSplit("cross", other, {inner, tile("tools")}, {0.6, 0.4});
            const auto root = makeSplit("outer", axis, {tile("mail"), makeStage("desk", cross), tile("files")},
                                        {0.25, 0.5, 0.25});
            const auto m = metrics();
            auto result = dragBoundary(root, m, "inside", 0);
            QVERIFY(result.valid);
            const auto pushed = result.drag.to(result.drag.max);
            QVERIFY(pushed != root);
            QCOMPARE(findNode(pushed, "cross")->weights, cross->weights);
            result.drag.to(result.drag.min);
            // Cancel restores identity, including every original stored weight.
            QVERIFY(result.drag.to(result.drag.start) == root);
            QVERIFY(findStage(root)->child == cross);
        }
    }

    void resizingOneBranchPreservesUnrelatedStoredWeights()
    {
        const auto untouched = makeSplit("shelf", Axis::X, {tile("mail"), tile("files"), tile("tools")},
                                         {0.01, 0.49, 0.5});
        const auto root = makeSplit("stack", Axis::Y,
                                    {makeSplit("work", Axis::X, {tile("notes"), tile("web")}, {0.5, 0.5}), untouched},
                                    {0.4, 0.6});
        const auto m = metrics();
        const auto before = layoutRects(root, &m);
        QVERIFY(before.at("mail").rect.w > untouched->weights.front());
        const auto result = dragBoundary(root, m, "work", 0);
        QVERIFY(result.valid);
        const auto moved = result.drag.to(0.72);
        QVERIFY(moved != root);
        QVERIFY(findNode(moved, "shelf") == untouched);
        QCOMPARE(moved->weights, root->weights);
        QVERIFY(sameRect(layoutRects(moved, &m).at("mail").rect, before.at("mail").rect));
    }

    void crampedGroupGrowsUniformlyAndCanRollBackExactly()
    {
        std::vector<NodePtr> children;
        for (int i = 0; i < 6; ++i)
            children.push_back(tile("task-" + std::to_string(i)));
        const auto cramped = makeSplit("tasks", Axis::X, children);
        const auto root = makeSplit("row", Axis::X, {tile("mail"), cramped}, {0.8, 0.2});
        const auto m = metrics();
        const auto before = layoutRects(root, &m);
        QVERIFY(before.at("task-0").scale < 1);
        const auto result = dragBoundary(root, m, "row", 0);
        QVERIFY(result.valid);
        QVERIFY(qAbs(result.drag.max - result.drag.start) < 1e-9);
        const auto grown = result.drag.to(0.4);
        const auto after = layoutRects(grown, &m);
        QVERIFY(qAbs(after.at("tasks").rect.x - 0.4) < 1e-9);
        for (const auto &child : children) {
            QVERIFY(qAbs(after.at(child->id).rect.w - 0.1) < 1e-9);
            QVERIFY(after.at(child->id).scale > before.at(child->id).scale);
        }
        QVERIFY(findNode(grown, "tasks") == cramped);
        QVERIFY(result.drag.to(result.drag.start) == root);
    }
};

QTEST_APPLESS_MAIN(TestMoreSemantics)
#include "test_more_semantics.moc"
