#include "tree.h"

#include <QtTest>

using namespace trellis;

static NodePtr P(const char *id) { return makePanel(id, {std::string(id) + "-v"}); }

class TestTree : public QObject
{
    Q_OBJECT
private slots:
    void layoutByWeights()
    {
        auto root = makeSplit("s", Axis::X, {P("a"), P("b")}, {1, 3});
        auto e = layoutRects(root);
        QCOMPARE(e.at("a").rect.w, 0.25);
        QCOMPARE(e.at("b").rect.x, 0.25);
        QCOMPARE(e.at("b").rect.w, 0.75);
        QCOMPARE(e.at("b").parent, std::string("s"));
    }
    void stageOccupiesChildRect()
    {
        auto root = makeSplit("s", Axis::X, {P("a"), makeStage("stage", P("b"))});
        auto e = layoutRects(root);
        QCOMPARE(e.at("stage").rect, e.at("b").rect);
    }
    void normalizeCollapsesAndFlattens()
    {
        auto inner = makeSplit("i", Axis::X, {P("b"), P("c")});
        auto root = makeSplit("r", Axis::X, {P("a"), inner}, {1, 1});
        auto n = normalize(root);
        QCOMPARE(n->children.size(), size_t(3));
        QCOMPARE(n->weights[0], 0.5);
        QCOMPARE(n->weights[1], 0.25);
        // single child collapses
        auto single = normalize(makeSplit("r", Axis::Y, {P("a")}));
        QCOMPARE(single->id, std::string("a"));
    }
    void normalizeKeepsIdentityWhenUnchanged()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")}, {0.5, 0.5});
        QVERIFY(normalize(root) == root);
    }
    void removeGivesShareToNext()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b"), P("c")}, {1, 2, 1});
        auto n = removeNode(root, "a");
        QCOMPARE(n->children.size(), size_t(2));
        QCOMPARE(n->weights[0], 0.75);
        // At the end the previous sibling takes it.
        auto m = removeNode(root, "c");
        QCOMPARE(m->weights[1], 0.75);
    }
    void removeKeepingProportionsSharesAmongAll()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b"), P("c")}, {1, 2, 1});
        auto n = removeNode(root, "b", true);
        QCOMPARE(n->weights[0], 0.5);
        QCOMPARE(n->weights[1], 0.5);
        auto m = removeNode(root, "a", true);
        QCOMPARE(m->weights[0], 2.0 / 3.0);
    }
    void removeKeepsStage()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), makeStage("stage")});
        auto n = removeNode(root, "stage");
        QVERIFY(n == root);
        QVERIFY(findStage(removeNode(makeStage("stage", P("x")), "x")));
    }
    void insertBesideClampsShare()
    {
        auto n = insertBeside(P("a"), "a", P("b"), Edge::Left, "s", 0.99);
        QCOMPARE(n->children[0]->id, std::string("b"));
        QCOMPARE(n->weights[0], 0.9);
    }
    void insertAlongSameAxisFlattens()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")});
        auto n = insertBeside(root, "b", P("c"), Edge::Right, "s2");
        QCOMPARE(n->children.size(), size_t(3));
        QCOMPARE(n->weights[2], 0.25);
    }
    void minimumsScaleDownWhenCramped()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")});
        LayoutMetrics m;
        m.width = 600;
        m.height = 400;
        m.min = [](const Node &, Axis a) { return a == Axis::X ? 480.0 : 320.0; };
        auto e = layoutRects(root, &m);
        // 960 needed in 600 px: the group lays out at 600/0.625 and scales by 0.625.
        QVERIFY(qAbs(e.at("a").scale - 0.625) < 1e-9);
        QVERIFY(qAbs(e.at("a").rect.w - 0.5) < 1e-9);
    }
    void resizeBoundaryRespectsMinimums()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")});
        auto n = resizeBoundary(root, 0, 0.99, [](int) { return 0.1; });
        QVERIFY(qAbs(n->weights[1] - 0.1) < 1e-9);
    }
};

QTEST_APPLESS_MAIN(TestTree)
#include "test_tree.moc"
