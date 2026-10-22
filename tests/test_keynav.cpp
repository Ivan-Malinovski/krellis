#include "keynav.h"
#include "spatial.h"

#include <QtTest>

using namespace krellis;

static NodePtr P(const char *id) { return makePanel(id, {std::string(id) + "-v"}); }

static LayoutMetrics metrics()
{
    LayoutMetrics m;
    m.width = 1000;
    m.height = 800;
    m.min = [](const Node &, Axis) { return 100.0; };
    return m;
}

static LayoutDocument docOf(NodePtr root)
{
    LayoutDocument d;
    d.root = std::move(root);
    return d;
}

class TestKeynav : public QObject
{
    Q_OBJECT
private slots:
    void gridFocus()
    {
        auto root = makeSplit("r", Axis::X, {makeSplit("l", Axis::Y, {P("a"), P("b")}), makeSplit("q", Axis::Y, {P("c"), P("d")})});
        const auto e = layoutRects(root);
        QCOMPARE(neighbourInDirection(e, "a", Edge::Right), std::string("c"));
        QCOMPARE(neighbourInDirection(e, "a", Edge::Bottom), std::string("b"));
        QCOMPARE(neighbourInDirection(e, "d", Edge::Left), std::string("b"));
        QCOMPARE(neighbourInDirection(e, "d", Edge::Top), std::string("c"));
    }
    void nothingThatWay()
    {
        const auto e = layoutRects(makeSplit("r", Axis::X, {P("a"), P("b")}));
        QCOMPARE(neighbourInDirection(e, "a", Edge::Left), std::string());
        QCOMPARE(neighbourInDirection(e, "a", Edge::Top), std::string());
        QCOMPARE(neighbourInDirection(e, "missing", Edge::Left), std::string());
    }
    void prefersTheNeighbourSharingMostExtent()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), makeSplit("s", Axis::Y, {P("b"), P("c")}, {1, 3})});
        const auto e = layoutRects(root);
        QCOMPARE(neighbourInDirection(e, "a", Edge::Right), std::string("c"));
        QCOMPARE(neighbourInDirection(e, "b", Edge::Left), std::string("a"));
        QCOMPARE(neighbourInDirection(e, "c", Edge::Left), std::string("a"));
    }
    void wideTileAboveTwoSmall()
    {
        auto root = makeSplit("r", Axis::Y, {P("t"), makeSplit("row", Axis::X, {P("x"), P("y")})});
        const auto e = layoutRects(root);
        QCOMPARE(neighbourInDirection(e, "y", Edge::Top), std::string("t"));
        QCOMPARE(neighbourInDirection(e, "x", Edge::Top), std::string("t"));
        QCOMPARE(neighbourInDirection(e, "t", Edge::Bottom), std::string("x"));
    }
    void aLoneParTradesPlaces()
    {
        // a and b are all there is to their split: moving toward the other swaps them, slot sizes stay.
        bool moved = false;
        auto d = moveInDirection(docOf(makeSplit("r", Axis::Y, {P("a"), P("b")}, {1, 3})), "b", Edge::Top, metrics(), &moved);
        QVERIFY(moved);
        const auto e = layoutRects(d.root);
        QVERIFY(e.at("b").rect.y < e.at("a").rect.y);
        QCOMPARE(e.at("b").rect.h, 0.25);
    }
    void siblingsAmongSeveralAreNotSwapped()
    {
        // b is 750x800 (tall) in a three-way split: a, level with its middle, goes under it.
        bool moved = false;
        auto d = moveInDirection(docOf(makeSplit("r", Axis::X, {P("a"), P("b"), P("c")}, {1, 3, 1})), "a", Edge::Right, metrics(), &moved);
        QVERIFY(moved);
        const auto e = layoutRects(d.root);
        QVERIFY(e.at("a").rect.y >= e.at("b").rect.y + e.at("b").rect.h - 1e-9);
    }
    void steppingIntoATallTileLandsUnderOrOverIt()
    {
        // b is 500x600 (tall): a, level with the column's middle, goes underneath, not beside.
        auto root = makeSplit("r", Axis::X, {P("a"), makeSplit("s", Axis::Y, {P("b"), P("c")}, {3, 1})});
        bool moved = false;
        auto d = moveInDirection(docOf(root), "a", Edge::Right, metrics(), &moved);
        QVERIFY(moved);
        QCOMPARE(panelsOf(d.root).size(), size_t(3));
        const auto e = layoutRects(d.root);
        QVERIFY(e.at("a").rect.y >= e.at("b").rect.y + e.at("b").rect.h - 1e-9);
        // Coming from the top half it lands above.
        auto top = makeSplit("r", Axis::X, {makeSplit("l", Axis::Y, {P("a"), P("z")}), P("t")});
        d = moveInDirection(docOf(top), "a", Edge::Right, metrics(), &moved);
        const auto f = layoutRects(d.root);
        QVERIFY(f.at("a").rect.y + f.at("a").rect.h <= f.at("t").rect.y + 1e-9);
    }
    void steppingIntoAWideTileLandsBesideIt()
    {
        // t is 1000x400 (wide): moving down into it, a docks on the side it came from, beside t.
        auto root = makeSplit("r", Axis::Y, {makeSplit("row", Axis::X, {P("a"), P("b")}), P("t")});
        bool moved = false;
        auto d = moveInDirection(docOf(root), "a", Edge::Bottom, metrics(), &moved);
        QVERIFY(moved);
        const auto e = layoutRects(d.root);
        QVERIFY(e.at("a").rect.x + e.at("a").rect.w <= e.at("t").rect.x + 1e-9);
        // Moving along the long side enters through the near edge: w1 is 500x400, a steps right into it.
        auto row = makeSplit("r", Axis::X, {P("a"), makeSplit("s", Axis::Y, {P("w1"), P("w2")})});
        d = moveInDirection(docOf(row), "a", Edge::Right, metrics(), &moved);
        QVERIFY(moved);
        const auto f = layoutRects(d.root);
        QVERIFY(f.at("a").rect.x + f.at("a").rect.w <= f.at("w1").rect.x + 1e-9);
        QVERIFY(f.at("a").rect.y + f.at("a").rect.h <= f.at("w2").rect.y + 1e-9);
    }
    void poppingOutOfAColumn()
    {
        auto root = makeSplit("r", Axis::X, {makeSplit("s", Axis::Y, {P("a"), P("c")}), P("b")});
        bool moved = false;
        auto d = moveInDirection(docOf(root), "a", Edge::Left, metrics(), &moved);
        QVERIFY(moved);
        const auto e = layoutRects(d.root);
        QCOMPARE(e.at("a").rect.x, 0.0);
        QVERIFY(e.at("c").rect.x > e.at("a").rect.x);
        QVERIFY(e.at("a").rect.h > 0.99);
    }
    void dockAtEdgeTakesTheWholeSide()
    {
        auto root = makeSplit("r", Axis::X, {makeSplit("s", Axis::Y, {P("a"), P("b")}), P("c")});
        const auto d = dockAtEdge(docOf(root), "b", Edge::Left);
        const auto e = layoutRects(d.root);
        QCOMPARE(e.at("b").rect.x, 0.0);
        QVERIFY(e.at("b").rect.h > 0.99);
        QCOMPARE(panelsOf(d.root).size(), size_t(3));
        // A lone panel has nowhere to go.
        QCOMPARE(dockAtEdge(docOf(P("only")), "only", Edge::Left).root->id, std::string("only"));
    }
    void flushPanelStaysPut()
    {
        bool moved = true;
        auto d = moveInDirection(docOf(makeSplit("r", Axis::X, {P("a"), P("b")})), "a", Edge::Left, metrics(), &moved);
        QVERIFY(!moved);
        QCOMPARE(d.root->children[0]->id, std::string("a"));
        moved = true;
        moveInDirection(docOf(P("only")), "only", Edge::Left, metrics(), &moved);
        QVERIFY(!moved);
    }
    void resizeGrowsTowardsTheArrow()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")}, {1, 1});
        const auto grown = resizeInDirection(root, metrics(), "a", Edge::Right, 0.1);
        QVERIFY(grown);
        QVERIFY(qAbs(layoutRects(grown).at("a").rect.w - 0.55) < 1e-6);
    }
    void resizeShrinksWhenNoNeighbourThatWay()
    {
        auto root = makeSplit("r", Axis::X, {P("a"), P("b")}, {1, 1});
        const auto shrunk = resizeInDirection(root, metrics(), "a", Edge::Left, 0.1);
        QVERIFY(shrunk);
        QVERIFY(qAbs(layoutRects(shrunk).at("a").rect.w - 0.45) < 1e-6);
    }
    void resizeUsesTheNearestAncestorDivider()
    {
        auto root = makeSplit("r", Axis::X, {makeSplit("s", Axis::Y, {P("a"), P("c")}), P("b")}, {1, 1});
        const auto grown = resizeInDirection(root, metrics(), "a", Edge::Right, 0.1);
        QVERIFY(grown);
        QVERIFY(qAbs(layoutRects(grown).at("a").rect.w - 0.55) < 1e-6);
        const auto taller = resizeInDirection(root, metrics(), "a", Edge::Bottom, 0.1);
        QVERIFY(taller);
        QVERIFY(layoutRects(taller).at("a").rect.h > 0.5);
    }
    void resizeStopsAtTheMinimum()
    {
        NodePtr root = makeSplit("r", Axis::X, {P("a"), P("b")}, {1, 1});
        int steps = 0;
        while (auto next = resizeInDirection(root, metrics(), "a", Edge::Right, 0.2)) {
            root = next;
            QVERIFY(++steps < 50);
        }
        QVERIFY(steps > 0);
        QVERIFY(layoutRects(root).at("b").rect.w * 1000 >= 100 - 1e-6);
    }
    void resizeWithoutADividerDoesNothing()
    {
        QVERIFY(!resizeInDirection(P("only"), metrics(), "only", Edge::Right, 0.1));
    }
    // Vacancies: the empty slot a window leaves when pulled out in float-first mode.
    void vacatingKeepsTheNeighboursPut()
    {
        LayoutDocument d = docOf(makeSplit("r", Axis::X, {P("a"), P("b"), P("c")}, {1, 2, 1}));
        const auto before = layoutRects(d.root);
        const auto vacated = vacatePanel(d, "b");
        QVERIFY(vacated);
        const auto after = layoutRects(vacated->root);
        QCOMPARE(after.at("a").rect.w, before.at("a").rect.w);
        QCOMPARE(after.at("c").rect.x, before.at("c").rect.x);
        const auto empty = vacancies(vacated->root);
        QCOMPARE(int(empty.size()), 1);
        QCOMPARE(after.at(empty.front()).rect.w, before.at("b").rect.w);
    }
    void aSoleTileHasNoSlotToKeep()
    {
        QVERIFY(!vacatePanel(docOf(P("only")), "only"));
    }
    void vacanciesSurviveAnotherRemoval()
    {
        auto vacated = *vacatePanel(docOf(makeSplit("r", Axis::X, {P("a"), P("b"), P("c")})), "b");
        const auto after = removePanel(vacated, "a");
        QCOMPARE(int(vacancies(after.root).size()), 1);
        QCOMPARE(int(leafIds(after.root).size()), 2);
    }
    void fillingTakesTheSlot()
    {
        auto vacated = *vacatePanel(docOf(makeSplit("r", Axis::X, {P("a"), P("b")})), "a");
        const auto before = layoutRects(vacated.root);
        const std::string slot = vacancies(vacated.root).front();
        const auto filled = fillVacancy(vacated, slot, P("n"));
        QVERIFY(vacancies(filled.root).empty());
        QCOMPARE(layoutRects(filled.root).at("n").rect.w, before.at(slot).rect.w);
    }
    void focusSkipsVacanciesButMovesTradeWithThem()
    {
        auto d = *vacatePanel(docOf(makeSplit("r", Axis::X, {P("a"), P("b"), P("c")})), "b");
        const std::string slot = vacancies(d.root).front();
        const auto e = layoutRects(d.root);
        QCOMPARE(neighbourInDirection(e, "a", Edge::Right), std::string("c"));
        QCOMPARE(neighbourInDirection(e, "a", Edge::Right, false), slot);
        bool moved = false;
        const auto next = moveInDirection(d, "c", Edge::Left, LayoutMetrics{}, &moved);
        QVERIFY(moved);
        QCOMPARE(int(vacancies(next.root).size()), 1);
    }
    void vacanciesAreNotKeptWhenSaved()
    {
        auto d = *vacatePanel(docOf(makeSplit("r", Axis::X, {P("a"), P("b")})), "b");
        QVERIFY(vacancies(sanitize(d).root).empty());
    }
};

QTEST_APPLESS_MAIN(TestKeynav)
#include "test_keynav.moc"
