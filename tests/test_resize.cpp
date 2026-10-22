// Port of the core cases from Trellis packages/core/test/resize-push.test.ts
#include "resize.h"

#include <QtTest>
#include <map>

using namespace krellis;
using Px = std::vector<double>;

static NodePtr panel(const char *id) { return makePanel(id, {id}); }
static NodePtr split(Axis a, const char *id, std::vector<NodePtr> c, std::vector<double> w = {})
{
    if (w.empty())
        w.assign(c.size(), 1.0);
    const double t = sum(w);
    for (auto &x : w)
        x /= t;
    return makeSplit(id, a, c, w);
}
static NodePtr row(const char *id, std::vector<NodePtr> c, std::vector<double> w = {}) { return split(Axis::X, id, c, w); }
static NodePtr column(const char *id, std::vector<NodePtr> c, std::vector<double> w = {}) { return split(Axis::Y, id, c, w); }

static LayoutMetrics metrics(std::map<std::string, double> mins = {}, double w = 1000, double h = 600)
{
    LayoutMetrics m;
    m.width = w;
    m.height = h;
    m.min = [mins](const Node &n, Axis a) {
        auto it = mins.find(n.id);
        if (it != mins.end())
            return it->second;
        if (n.kind == Node::Kind::Stage && !n.child)
            return 120.0;
        return a == Axis::X ? 100.0 : 50.0;
    };
    return m;
}

static double round6(double v) { return std::round(v * 1e6) / 1e6; }

static Px bounds(const NodePtr &root, const LayoutMetrics &m, const char *id)
{
    auto e = layoutRects(root, &m);
    auto node = findNode(root, id);
    const Rect r = e.at(id).rect;
    const bool x = node->axis == Axis::X;
    const double px = x ? m.width : m.height;
    Px out{round6((x ? r.x : r.y) * px)};
    for (size_t i = 1; i < node->children.size(); ++i) {
        const Rect cr = e.at(node->children[i]->id).rect;
        out.push_back(round6((x ? cr.x : cr.y) * px));
    }
    out.push_back(round6(((x ? r.x : r.y) + (x ? r.w : r.h)) * px));
    return out;
}

static NodePtr drag(const NodePtr &root, const LayoutMetrics &m, const char *id, int index, double to)
{
    auto d = dragBoundary(root, m, id, index);
    const double px = findNode(root, id)->axis == Axis::X ? m.width : m.height;
    return d.drag.to(to / px);
}

class TestResize : public QObject
{
    Q_OBJECT
private slots:
    void oneRow()
    {
        auto m = metrics();
        auto abc = row("r", {panel("a"), panel("b"), panel("c")}, {300, 300, 400});
        QCOMPARE(bounds(drag(abc, m, "r", 0, 350), m, "r"), (Px{0, 350, 600, 1000}));
        QCOMPARE(bounds(drag(abc, m, "r", 0, 700), m, "r"), (Px{0, 700, 800, 1000}));
        QCOMPARE(bounds(drag(abc, m, "r", 0, 950), m, "r"), (Px{0, 800, 900, 1000}));
        QCOMPARE(bounds(drag(abc, m, "r", 1, 20), m, "r"), (Px{0, 100, 200, 1000}));
        QCOMPARE(bounds(drag(abc, m, "r", 1, 150), m, "r"), (Px{0, 100, 200, 1000}));
        QCOMPARE(bounds(drag(abc, m, "r", 1, 250), m, "r"), (Px{0, 150, 250, 1000}));
    }
    void reach()
    {
        auto m = metrics();
        auto abc = row("r", {panel("a"), panel("b"), panel("c")}, {300, 300, 400});
        auto d = dragBoundary(abc, m, "r", 0);
        QVERIFY(d.valid);
        QVERIFY(d.drag.axis == Axis::X);
        QVERIFY(qAbs(d.drag.start * 1000 - 300) < 1e-6);
        QVERIFY(qAbs(d.drag.min * 1000 - 100) < 1e-6);
        QVERIFY(qAbs(d.drag.max * 1000 - 800) < 1e-6);
    }
    void nearestFirst()
    {
        auto m = metrics();
        auto four = row("r", {panel("a"), panel("b"), panel("c"), panel("d")});
        QCOMPARE(bounds(drag(four, m, "r", 0, 400), m, "r"), (Px{0, 400, 500, 750, 1000}));
        QCOMPARE(bounds(drag(four, m, "r", 0, 500), m, "r"), (Px{0, 500, 600, 750, 1000}));
        QCOMPARE(bounds(drag(four, m, "r", 0, 700), m, "r"), (Px{0, 700, 800, 900, 1000}));
    }
    void draggingBackUndoesPushes()
    {
        auto m = metrics();
        auto abc = row("r", {panel("a"), panel("b"), panel("c")}, {300, 300, 400});
        auto d = dragBoundary(abc, m, "r", 0);
        d.drag.to(0.9);
        d.drag.to(0.7);
        QCOMPARE(bounds(d.drag.to(d.drag.start), m, "r"), (Px{0, 300, 600, 1000}));
    }
    void keepPushed()
    {
        auto m = metrics();
        auto abc = row("r", {panel("a"), panel("b"), panel("c")}, {300, 300, 400});
        auto pushed = drag(abc, m, "r", 0, 700);
        QCOMPARE(bounds(drag(pushed, m, "r", 0, 300), m, "r"), (Px{0, 300, 800, 1000}));
    }
    void column_()
    {
        auto m = metrics();
        auto col = column("col", {panel("a"), panel("b"), panel("c")});
        QCOMPARE(bounds(drag(col, m, "col", 0, 500), m, "col"), (Px{0, 500, 550, 600}));
    }
    void bubblesUpThroughTree()
    {
        auto m = metrics();
        auto tree = row("r", {panel("A"), column("col", {row("inner", {panel("B"), panel("C")}), panel("D")}), panel("E")},
                        {1, 2, 1});
        auto t = drag(tree, m, "inner", 0, 700);
        QCOMPARE(bounds(t, m, "inner"), (Px{250, 700, 800}));
        QCOMPARE(bounds(t, m, "r"), (Px{0, 250, 800, 1000}));
        auto d = dragBoundary(tree, m, "inner", 0);
        QVERIFY(qAbs(d.drag.max * 1000 - 800) < 1e-6);
        auto left = drag(tree, m, "inner", 0, 200);
        QCOMPARE(bounds(left, m, "inner"), (Px{100, 200, 750}));
        QCOMPARE(bounds(left, m, "r"), (Px{0, 100, 750, 1000}));
    }
    void growingSideChangesAsLittleAsPossible()
    {
        auto m = metrics();
        auto t = row("r", {column("col", {row("inner", {panel("A1"), panel("A2")}), panel("G")}), panel("B")});
        auto moved = drag(t, m, "r", 0, 700);
        QCOMPARE(bounds(moved, m, "inner"), (Px{0, 250, 700}));
        QCOMPARE(bounds(moved, m, "r"), (Px{0, 700, 1000}));
    }
    void invalidDrag()
    {
        auto m = metrics();
        auto abc = row("r", {panel("a"), panel("b")});
        QVERIFY(!dragBoundary(abc, m, "r", 1).valid);
        QVERIFY(!dragBoundary(abc, m, "nope", 0).valid);
    }
};

QTEST_APPLESS_MAIN(TestResize)
#include "test_resize.moc"
