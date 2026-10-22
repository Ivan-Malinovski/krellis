// Port of Trellis packages/core/test/model.test.ts
#include "builder.h"
#include "document.h"
#include "spatial.h"

#include <QtTest>

using namespace krellis;
namespace L = krellis::layout;

static NodePtr P(const char *id, std::vector<std::string> views) { return makePanel(id, std::move(views)); }

static LayoutDocument ide()
{
    auto docs = L::panel({L::view("editor", "a"), L::view("editor", "b")}, "docs");
    auto root = L::row({L::panel({L::view("files", "files"), L::view("search", "search")}, "left"),
                        L::stage(&docs, "stage"), L::panel({L::view("outline", "outline")}, "outline")},
                       {1, 3, 1});
    return createDocument(&root);
}

static std::vector<std::string> ids(const std::vector<NodePtr> &ns)
{
    std::vector<std::string> out;
    for (auto &n : ns)
        out.push_back(n->id);
    return out;
}
using V = std::vector<std::string>;

class TestModel : public QObject
{
    Q_OBJECT
private slots:
    void builderCompiles()
    {
        auto doc = ide();
        auto v = viewIds(doc);
        std::sort(v.begin(), v.end());
        QCOMPARE(v, (V{"a", "b", "files", "outline", "search"}));
        QCOMPARE(doc.views["a"].type, std::string("editor"));
        QCOMPARE(findStage(doc.root)->id, std::string("stage"));
        QCOMPARE(doc.root->weights[1], 0.6);
    }
    void builderRejectsDuplicates()
    {
        auto bad = L::row({L::view("x", "a"), L::view("x", "a")});
        QVERIFY_EXCEPTION_THROWN(createDocument(&bad), std::runtime_error);
        auto two = L::row({L::stage(), L::stage()});
        QVERIFY_EXCEPTION_THROWN(createDocument(&two), std::runtime_error);
    }
    void builderDeterministicIds()
    {
        auto spec = L::row({L::view("x"), L::view("x", "x-2"), L::view("x"), L::view("y")});
        auto a = viewIds(createDocument(&spec));
        QCOMPARE(a, (V{"x-1", "x-2", "x-3", "y-1"}));
        QCOMPARE(viewIds(createDocument(&spec)), a);
    }
    void treeLayout()
    {
        auto rects = layoutRects(ide().root);
        QCOMPARE(rects.at("left").rect.x, 0.0);
        QVERIFY(qAbs(rects.at("left").rect.w - 0.2) < 1e-12);
        QVERIFY(qAbs(rects.at("docs").rect.x - 0.2) < 1e-12);
        QVERIFY(rects.at("stage").rect == rects.at("docs").rect);
    }
    void normalizeFlattens()
    {
        auto inner = makeSplit("inner", Axis::X, {P("b", {"2"}), P("c", {"3"})}, {0.5, 0.5});
        auto outer = makeSplit("outer", Axis::X, {P("a", {"1"}), inner}, {0.5, 0.5});
        auto n = normalize(outer);
        QCOMPARE(ids(n->children), (V{"a", "b", "c"}));
        QCOMPARE(n->weights, (std::vector<double>{0.5, 0.25, 0.25}));
    }
    void removeKeepsStage()
    {
        auto doc = ide();
        auto without = removeNode(doc.root, "left");
        QCOMPARE(ids(without->children), (V{"stage", "outline"}));
        QVERIFY(qAbs(without->weights[0] - 0.8) < 1e-12);
        QVERIFY(removeNode(doc.root, "stage") == doc.root);
        auto emptied = removeNode(doc.root, "docs");
        QVERIFY(!findStage(emptied)->child);
    }
    void insertBesideShare()
    {
        auto root = insertBeside(P("a", {"1"}), "a", P("b", {"2"}), Edge::Left, "s", 0.25);
        QCOMPARE(ids(root->children), (V{"b", "a"}));
        QCOMPARE(root->weights, (std::vector<double>{0.25, 0.75}));
        auto below = insertBeside(root, "a", P("c", {"3"}), Edge::Bottom, "t");
        QVERIFY(findNode(below, "t")->axis == Axis::Y);
    }
    void closeViewKeepsStage()
    {
        auto doc = closeView(ide(), "a");
        QCOMPARE(locatePanel(doc, "docs").panel->selected, std::string("b"));
        doc = closeView(doc, "b");
        QVERIFY(!locatePanel(doc, "docs"));
        QVERIFY(findStage(doc.root) && !findStage(doc.root)->child);
        QVERIFY(!doc.views.count("a"));
    }
    void closeFocusGoesToNearestSibling()
    {
        auto root = L::row({L::panel({L::view("t", "a")}, "pa"),
                            L::column({L::panel({L::view("t", "b")}, "pb"), L::panel({L::view("t", "c")}, "pc")}),
                            L::panel({L::view("t", "d")}, "pd")});
        const auto doc = createDocument(&root);
        // The previous sibling wins; the first window falls back to the next one, and a group yields the leaf next to the gap.
        QCOMPARE(closeFocusPanel(doc, "d"), std::string("pc"));
        QCOMPARE(closeFocusPanel(doc, "a"), std::string("pb"));
        QCOMPARE(closeFocusPanel(doc, "b"), std::string("pc"));
        QCOMPARE(closeFocusPanel(doc, "c"), std::string("pb"));
        // A window with another tab keeps focus in its own panel, and a lone window has nobody to hand it to.
        auto tabs = L::row({L::panel({L::view("t", "a"), L::view("t", "x")}, "pa"), L::panel({L::view("t", "b")}, "pb")});
        QVERIFY(closeFocusPanel(createDocument(&tabs), "a").empty());
        auto alone = L::row({L::panel({L::view("t", "a")}, "pa")});
        QVERIFY(closeFocusPanel(createDocument(&alone), "a").empty());
    }
    void closeSharesProportionally()
    {
        auto root = L::row({L::panel({L::view("t", "a")}, "pa"), L::panel({L::view("t", "b")}, "pb"),
                            L::panel({L::view("t", "c")}, "pc")},
                           {1, 2, 1});
        auto doc = closeView(createDocument(&root), "b");
        QCOMPARE(doc.root->weights.size(), size_t(2));
        QVERIFY(qAbs(doc.root->weights[0] - 0.5) < 1e-12);
        // Open a column at the end, then close it again: the three columns are equal once more.
        root = L::row({L::panel({L::view("t", "a")}, "pa"), L::panel({L::view("t", "b")}, "pb"),
                       L::panel({L::view("t", "c")}, "pc")});
        doc = createDocument(&root);
        doc.views["n"] = ViewRecord{"t", {}, {}};
        doc.root = applyDockTarget(doc.root, seamTarget(doc.root, 3), P("pn", {"n"}), "sn");
        QCOMPARE(doc.root->children.size(), size_t(4));
        doc = closeView(doc, "n");
        QCOMPARE(doc.root->children.size(), size_t(3));
        for (double w : doc.root->weights)
            QVERIFY(qAbs(w - 1.0 / 3) < 1e-9);
        // Detaching (a tab move, a transfer to another canvas's tab) still lets the next sibling absorb.
        root = L::row({L::panel({L::view("t", "a")}, "pa"), L::panel({L::view("t", "b")}, "pb"),
                       L::panel({L::view("t", "c")}, "pc")});
        doc = detachView(createDocument(&root), "b");
        QVERIFY(qAbs(doc.root->weights[0] - 1.0 / 3) < 1e-9);
        QVERIFY(qAbs(doc.root->weights[1] - 2.0 / 3) < 1e-9);
    }
    void detachAndAddTab()
    {
        auto doc = detachView(ide(), "b");
        QVERIFY(doc.views.count("b"));
        doc = addTab(doc, "left", "b", 1);
        auto p = locatePanel(doc, "left").panel;
        QCOMPARE(p->views, (V{"files", "b", "search"}));
        QCOMPARE(p->selected, std::string("b"));
    }
    void selectAndReorder()
    {
        auto doc = selectView(ide(), "b");
        QCOMPARE(panelOfView(doc, "b")->selected, std::string("b"));
        doc = reorderTabs(doc, "docs", {"b", "a"});
        QCOMPARE(panelOfView(doc, "a")->views, (V{"b", "a"}));
    }
    void dockIntoStage()
    {
        auto doc = closeView(closeView(ide(), "a"), "b");
        doc.views["n"] = ViewRecord{"editor", {}, {}};
        doc = insertPanel(doc, P("p1", {"n"}), DockTarget::intoPanel("stage"));
        QCOMPARE(findStage(doc.root)->child->id, std::string("p1"));
        doc.views["m"] = ViewRecord{"editor", {}, {}};
        doc = insertPanel(doc, P("p2", {"m"}), DockTarget::intoPanel("stage"));
        QCOMPARE(locatePanel(doc, "p1").panel->views, (V{"n", "m"}));
    }
    void hideRestoreFloat()
    {
        auto doc = ide();
        doc = hidePanel(doc, "outline");
        QVERIFY(doc.hidden[0].restore.kind == RestoreTarget::Kind::Docked);
        QCOMPARE(doc.hidden[0].restore.beside, std::string("stage"));
        QVERIFY(doc.hidden[0].restore.edge == Edge::Right);
        doc = restorePanel(doc, "outline", FloatingLayer::Overlay);
        QCOMPARE(ids(doc.root->children), (V{"left", "stage", "outline"}));
        QVERIFY(qAbs(doc.root->weights[2] - 0.2) < 1e-9);
        auto floating = floatPanel(doc, locatePanel(doc, "outline").panel, {0.1, 0.1, 0.3, 0.3}, FloatingLayer::Overlay);
        QCOMPARE(floating.floating.size(), size_t(1));
        auto hidden = hidePanel(floating, "outline");
        QVERIFY(hidden.hidden[0].restore.kind == RestoreTarget::Kind::Floating);
        QCOMPARE(restorePanel(hidden, "outline", FloatingLayer::Overlay).floating.size(), size_t(1));
    }
    void sanitizeBroken()
    {
        LayoutDocument d;
        d.root = makeSplit("s", Axis::X,
                           {P("p", {"a", "ghost"}), P("q", {"a"}), makeStage("st1"), makeStage("st2")},
                           {std::nan(""), 2, 1, 1});
        d.floating.push_back({P("f", {"c"}), {5, -1, 3, 0}, 1, FloatingLayer::Overlay});
        d.views = {{"a", {"x", {}, {}}}, {"c", {"x", {}, {}}}, {"orphan", {"x", {}, {}}}};
        auto doc = sanitize(d);
        auto v = viewIds(doc);
        std::sort(v.begin(), v.end());
        QCOMPARE(v, (V{"a", "c"}));
        QVERIFY(!doc.views.count("orphan"));
        QCOMPARE(ids(panelsOf(doc.root)), (V{"p"}));
        QVERIFY(doc.floating[0].rect.w <= 1);
    }
    void dropGeometry()
    {
        Rect r{0, 0, 100, 100};
        QVERIFY(dropEdge({10, 50}, r) == Edge::Left);
        QVERIFY(!dropEdge({50, 50}, r));
        QVERIFY(dropEdge({50, 95}, r) == Edge::Bottom);
        // Middle of the tile: the longer side decides; near an edge, that edge.
        QVERIFY(pointerSpawnEdge({45, 50}, r, 100, 100) == Edge::Left);
        QVERIFY(pointerSpawnEdge({55, 50}, r, 100, 100) == Edge::Right);
        QVERIFY(pointerSpawnEdge({50, 45}, r, 100, 200) == Edge::Top);
        QVERIFY(pointerSpawnEdge({50, 55}, r, 100, 200) == Edge::Bottom);
        QVERIFY(pointerSpawnEdge({50, 55}, r, 200, 100) == Edge::Right);
        QVERIFY(pointerSpawnEdge({10, 50}, r, 100, 200) == Edge::Left);
        auto d = dropRects(r, Edge::Right);
        QVERIFY(d.slot == (Rect{50, 0, 50, 100}));
        QVERIFY(d.collapsed == (Rect{100, 0, 0, 100}));
    }
    void focusLayoutRangesAndFit()
    {
        auto root = makeSplit("r", Axis::X, {P("a", {"1"}), P("b", {"2"}), P("c", {"3"})});
        auto e = focusLayout(root);
        // a+b, b+c ranges exist; a+b+c is the root itself.
        QVERIFY(e.has(rangeId("r", {"a", "b"})));
        QVERIFY(e.has(rangeId("r", {"b", "c"})));
        QCOMPARE(e.size(), size_t(6));
        QCOMPARE(bestFit(Rect{0, 0, 1.0 / 3, 1}, e), std::string("a"));
        QCOMPARE(bestFit(Rect{0, 0, 2.0 / 3, 1}, e), rangeId("r", {"a", "b"}));
        QCOMPARE(frameLeaves(root, {"b", "c"}), rangeId("r", {"b", "c"}));
    }
    void untabAllSplitsEveryTabbedPanel()
    {
        auto root = L::row({L::panel({L::view("t", "a"), L::view("t", "b"), L::view("t", "c")}, "one"), L::panel({L::view("t", "d")}, "two")});
        auto doc = createDocument(&root);
        doc = selectView(doc, "b");
        doc = untabAll(doc);
        QCOMPARE(panelOfView(doc, "b")->id, std::string("one"));
        QCOMPARE(panelOfView(doc, "b")->views, (V{"b"}));
        for (const char *v : {"a", "c", "d"})
            QCOMPARE(panelOfView(doc, v)->views.size(), size_t(1));
        QVERIFY(panelOfView(doc, "a")->id != panelOfView(doc, "c")->id);
        auto seen = viewIds(doc);
        std::sort(seen.begin(), seen.end());
        QCOMPARE(seen, (V{"a", "b", "c", "d"}));
        QCOMPARE(untabAll(doc).root->kind, doc.root->kind); // nothing left to split
    }
    void untabAllKeepsFloatsAndHiddenPanels()
    {
        auto root = L::row({L::panel({L::view("t", "a")}, "one"), L::panel({L::view("t", "x"), L::view("t", "y")}, "two")});
        auto doc = createDocument(&root);
        doc.views["f"] = {"t", "f", {}};
        doc.views["g"] = {"t", "g", {}};
        doc = floatPanel(doc, makePanel("fl", {"f", "g"}, "f"), {0.2, 0.2, 0.4, 0.4}, FloatingLayer::Overlay);
        doc = hidePanel(doc, "two");
        doc = untabAll(doc);
        for (const auto &p : allPanels(doc))
            QCOMPARE(p->views.size(), size_t(1));
        QCOMPARE(doc.floating.size(), size_t(2));
        QCOMPARE(doc.hidden.size(), size_t(2));
    }
    void untabViewSplitsOneAndTabIntoMergesBack()
    {
        auto root = L::row({L::panel({L::view("t", "a"), L::view("t", "b"), L::view("t", "c")}, "one"), L::panel({L::view("t", "d")}, "two")});
        auto doc = createDocument(&root);
        doc = untabView(doc, "b");
        QCOMPARE(panelOfView(doc, "b")->views, (V{"b"}));
        QCOMPARE(panelOfView(doc, "a")->views, (V{"a", "c"}));
        QCOMPARE(untabView(doc, "b").root->kind, doc.root->kind); // already alone: nothing to split
        QCOMPARE(allPanels(doc).size(), size_t(3));
        // d joins the group as its selected tab, and its old tile closes
        doc = tabInto(doc, "d", "one");
        QCOMPARE(panelOfView(doc, "d")->id, std::string("one"));
        QCOMPARE(panelOfView(doc, "d")->selected, std::string("d"));
        QCOMPARE(allPanels(doc).size(), size_t(2));
        QVERIFY(!locatePanel(doc, "two"));
        QCOMPARE(tabInto(doc, "a", "one").root->kind, doc.root->kind); // into its own panel: nothing
    }
    void maximizeRestoresExactPrior()
    {
        auto root = makeSplit("r", Axis::X, {P("a", {"1"}), P("b", {"2"}), P("c", {"3"})});
        auto e = focusLayout(root);
        auto up = maximizeTransition(e, "r", rangeId("r", {"a", "b"}), "a", std::nullopt);
        QCOMPARE(up.destination, std::string("a"));
        QVERIFY(up.session);
        auto back = maximizeTransition(e, "r", "a", "a", up.session);
        QCOMPARE(back.destination, rangeId("r", {"a", "b"}));
    }
    void historyReplacesForward()
    {
        auto h = recordVisit({{"", {}}}, 0, {"a", {"a"}});
        h = recordVisit(h.visits, h.index, {"b", {"b"}});
        h = recordVisit(h.visits, 1, {"c", {"c"}});
        // Visiting "c" after going back to "a" drops "b" from forward history.
        QCOMPARE(h.visits.size(), size_t(3));
        QCOMPARE(h.index, 2);
        auto same = recordVisit(h.visits, h.index, {"c", {"c"}});
        QCOMPARE(same.visits.size(), size_t(3));
    }
};

QTEST_APPLESS_MAIN(TestModel)
#include "test_model.moc"
