#include "tree.h"

#include <QtTest>

using namespace krellis;

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
    void flipTogglesATwoSplitInPlace()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")}, {1, 3});
        auto flipped = flipBoundary(root, "r", 0, "w");
        QCOMPARE(flipped->axis, Axis::Y);
        QCOMPARE(flipped->children[0]->id, std::string("a"));
        QCOMPARE(flipped->weights[1], 0.75);
        QCOMPARE(flipBoundary(flipped, "r", 0, "w")->axis, Axis::X);
    }
    void flipWrapsAPairInALongerSplitAndUndoes()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b"), P("c")}, {1, 1, 2});
        auto flipped = flipBoundary(root, "r", 1, "w");
        QCOMPARE(flipped->children.size(), size_t(2));
        QCOMPARE(flipped->children[1]->id, std::string("w"));
        QCOMPARE(flipped->children[1]->axis, Axis::Y);
        QCOMPARE(flipped->weights[1], 0.75);
        // The pair is stacked in the right two thirds; flipping the wrapper again dissolves it.
        auto back = flipBoundary(flipped, "w", 0, "x");
        QCOMPARE(back->children.size(), size_t(3));
        QCOMPARE(back->axis, Axis::X);
        QCOMPARE(back->weights[0], 0.25);
        QCOMPARE(back->weights[2], 0.5);
    }
    void flipIgnoresABadBoundary()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")});
        QVERIFY(flipBoundary(root, "r", 1, "w") == root);
        QVERIFY(flipBoundary(root, "nope", 0, "w") == root);
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
    // Dwindle (Hyprland): splitting a tile of a row keeps the new pair as a group, so closing one of them gives the
    // other the pair's whole share and the rest of the row does not change.
    void nestedSplitStaysAGroupAndTheSiblingTakesItsShare()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")});
        // Focus a, open c on its right: a | c | b at 25 / 25 / 50.
        auto split = insertBeside(root, "a", P("c"), Edge::Right, "s", 0.5, true);
        QCOMPARE(split->children.size(), size_t(2));
        QVERIFY(split->children[0]->nested);
        auto e = layoutRects(split);
        QVERIFY(qAbs(e.at("a").rect.w - 0.25) < 1e-9);
        QVERIFY(qAbs(e.at("c").rect.x - 0.25) < 1e-9);
        QVERIFY(qAbs(e.at("b").rect.x - 0.5) < 1e-9);
        // Closing c gives a the whole half; the following sibling b is untouched. The plain rule would give c's share to b.
        auto closed = layoutRects(removeNode(split, "c"));
        QVERIFY(qAbs(closed.at("a").rect.w - 0.5) < 1e-9);
        QVERIFY(qAbs(closed.at("b").rect.w - 0.5) < 1e-9);
        auto plain = layoutRects(removeNode(insertBeside(root, "a", P("c"), Edge::Right, "s"), "c"));
        QVERIFY(qAbs(plain.at("a").rect.w - 0.25) < 1e-9);
        // Closing a hands the space to c, the window a was split with.
        auto lost = layoutRects(removeNode(split, "a"));
        QVERIFY(qAbs(lost.at("c").rect.w - 0.5) < 1e-9);
    }
    void nestedSplitOnTheNearSideAndAcrossAxes()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")});
        // Opening before b (cursor in its left half): a | c | b with c and b sharing b's half.
        auto split = insertBeside(root, "b", P("c"), Edge::Left, "s", 0.5, true);
        auto e = layoutRects(split);
        QVERIFY(qAbs(e.at("c").rect.x - 0.5) < 1e-9);
        QVERIFY(qAbs(e.at("b").rect.x - 0.75) < 1e-9);
        // A split along the other axis is a plain child either way, and a plain pair still flattens.
        auto stacked = insertBeside(root, "b", P("d"), Edge::Bottom, "t", 0.5, true);
        QCOMPARE(stacked->children[1]->axis, Axis::Y);
        auto flat = insertBeside(root, "b", P("e"), Edge::Right, "u");
        QCOMPARE(flat->children.size(), size_t(3));
    }
    void nestedSurvivesNormalizeAndFlip()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), makeSplit("g", Axis::X, {P("b"), P("c")}, {}, true)});
        QCOMPARE(normalize(root)->children.size(), size_t(2));
        QVERIFY(normalize(root)->children[1]->nested);
        auto flipped = flipBoundary(root, "g", 0, "w");
        QCOMPARE(findNode(flipped, "g")->axis, Axis::Y);
        QVERIFY(findNode(flipped, "g")->nested);
    }
};

QTEST_APPLESS_MAIN(TestTree)
#include "test_tree.moc"
