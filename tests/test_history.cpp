#include "document.h"
#include "history.h"
#include "reconcile.h"
#include "spatial.h"

#include <QtTest>

using namespace krellis;

static NodePtr P(const char *id, const char *view) { return makePanel(id, {view}); }

static LayoutDocument row(std::vector<NodePtr> panels)
{
    LayoutDocument doc;
    doc.root = makeSplit("root", Axis::X, std::move(panels));
    return doc; // live documents carry no view records
}

static std::vector<std::string> leaves(const LayoutDocument &doc) { return doc.root ? leafIds(doc.root) : std::vector<std::string>{}; }

class TestHistory : public QObject
{
    Q_OBJECT
private slots:
    void undoRedoRoundTrip()
    {
        History h;
        const auto a = row({P("pa", "a"), P("pb", "b")});
        const auto b = row({P("pb", "b"), P("pa", "a")});
        h.record(a, "move");
        QVERIFY(h.canUndo() && !h.canRedo());
        const auto u = h.undo(b);
        QVERIFY(u);
        QCOMPARE(u->label, std::string("move"));
        QVERIFY(sameLayout(u->doc, a));
        QVERIFY(h.canRedo() && !h.canUndo());
        const auto r = h.redo(a);
        QVERIFY(r);
        QVERIFY(sameLayout(r->doc, b));
        QVERIFY(h.canUndo());
        QVERIFY(!History().undo(a));
        QVERIFY(!History().redo(a));
    }
    void newEditDropsRedo()
    {
        History h;
        const auto a = row({P("pa", "a")});
        h.record(a, "one");
        h.undo(a);
        QVERIFY(h.canRedo());
        h.record(a, "two");
        QVERIFY(!h.canRedo());
    }
    void capDropsOldest()
    {
        History h(3);
        for (int i = 0; i < 5; ++i)
            h.record(row({P("pa", "a")}), "e" + std::to_string(i));
        QCOMPARE(int(h.size()), 3);
        QCOMPARE(h.undoLabel(), std::string("e4"));
    }
    void coalescesRepeatedEditsInsideTheWindow()
    {
        History h(50, 600);
        const auto first = row({P("pa", "a")});
        h.record(first, "resize", "resize:pa", 1000);
        h.record(row({P("pb", "b")}), "resize", "resize:pa", 1400);
        h.record(row({P("pc", "c")}), "resize", "resize:pa", 1900); // 500 ms after the previous one
        QCOMPARE(int(h.size()), 1);
        h.record(row({P("pd", "d")}), "resize", "resize:pa", 3000); // a pause starts a new step
        QCOMPARE(int(h.size()), 2);
        h.record(row({P("pe", "e")}), "resize", "resize:pz", 3100); // another panel too
        QCOMPARE(int(h.size()), 3);
        const auto u = h.undo(first);
        QVERIFY(u);
        QVERIFY(u->doc.root->children[0]->id == "pe");
    }
    void sameLayoutNoticesEveryPart()
    {
        auto a = row({P("pa", "a"), P("pb", "b")});
        auto b = a;
        QVERIFY(sameLayout(a, b));
        b.root = makeSplit("root", Axis::X, {P("pa", "a"), P("pb", "b")});
        QVERIFY(!sameLayout(a, b));
        auto c = a;
        c = floatPanel(c, P("pf", "f"), {0.1, 0.1, 0.3, 0.3}, FloatingLayer::Overlay);
        QVERIFY(!sameLayout(a, c));
        auto d = c;
        d.floating[0].rect.x = 0.2;
        QVERIFY(!sameLayout(c, d));
        QVERIFY(!sameLayout(a, hidePanel(a, "pa")));
    }

    void reconcileDropsClosedWindows()
    {
        const auto snap = row({P("pa", "a"), P("pb", "b"), P("pc", "c")});
        const auto now = row({P("pa", "a"), P("pc", "c")}); // b closed after the snapshot
        const auto out = reconcile(snap, now);
        QCOMPARE(leaves(out), (std::vector<std::string>{"pa", "pc"}));
    }
    void reconcileKeepsTheSnapshotArrangement()
    {
        const auto snap = row({P("pc", "c"), P("pa", "a"), P("pb", "b")});
        const auto now = row({P("pa", "a"), P("pb", "b"), P("pc", "c")});
        const auto out = reconcile(snap, now);
        QCOMPARE(leaves(out), (std::vector<std::string>{"pc", "pa", "pb"}));
        QVERIFY(sameLayout(out, snap));
    }
    void reconcileKeepsNewWindowsWhereTheyAre()
    {
        const auto snap = row({P("pa", "a"), P("pb", "b")});
        auto now = row({P("pa", "a"), P("pb", "b"), P("pn", "n")});
        now = floatPanel(now, P("pf", "f"), {0.2, 0.2, 0.3, 0.3}, FloatingLayer::Overlay);
        now = hidePanel(now, "pb");
        now.floatMargin = {0.1, 0.2, 0.3, 0.4};
        // pb is hidden now but was docked in the snapshot: it follows the compositor.
        const auto out = reconcile(snap, now);
        QCOMPARE(leaves(out), (std::vector<std::string>{"pa", "pn"}));
        QCOMPARE(int(out.floating.size()), 1);
        QCOMPARE(out.floating[0].panel->id, std::string("pf"));
        QCOMPARE(int(out.hidden.size()), 1);
        QCOMPARE(out.hidden[0].panel->id, std::string("pb"));
        QVERIFY(out.floatMargin == now.floatMargin);
    }
    void reconcileDoesNotUnhideOrRehideAgainstTheCompositor()
    {
        auto snap = hidePanel(row({P("pa", "a"), P("pb", "b")}), "pb"); // minimized when the snapshot was taken
        auto now = row({P("pa", "a"), P("pb", "b")});                   // restored since
        const auto out = reconcile(snap, now);
        QCOMPARE(int(out.hidden.size()), 0);
        auto l = leaves(out);
        std::sort(l.begin(), l.end());
        QCOMPARE(l, (std::vector<std::string>{"pa", "pb"}));
    }
    void reconcileUndoesAFloatMove()
    {
        auto base = floatPanel(row({P("pa", "a")}), P("pf", "f"), {0.1, 0.1, 0.3, 0.3}, FloatingLayer::Overlay);
        auto moved = base;
        moved.floating[0].rect.x = 0.5;
        const auto out = reconcile(base, moved);
        QCOMPARE(out.floating[0].rect.x, base.floating[0].rect.x);
    }
    void reconcileTakesScratchPanelsFromNow()
    {
        const auto snap = row({P("pa", "a"), P("ps", "s")}); // s was tiled when the snapshot was taken
        auto now = floatPanel(row({P("pa", "a")}), P("ps", "s"), {0.2, 0.2, 0.4, 0.4}, FloatingLayer::Overlay);
        const auto out = reconcile(snap, now, {"ps"});
        QCOMPARE(leaves(out), (std::vector<std::string>{"pa"}));
        QCOMPARE(int(out.floating.size()), 1);
    }
    void reconcileKeepsFloatStackingFromNow()
    {
        auto base = floatPanel(floatPanel(row({P("pa", "a")}), P("p1", "1"), {0.1, 0.1, 0.2, 0.2}, FloatingLayer::Overlay),
                               P("p2", "2"), {0.4, 0.1, 0.2, 0.2}, FloatingLayer::Overlay);
        const auto now = raiseFloat(base, "p1");
        const auto out = reconcile(base, now);
        int z1 = 0, z2 = 0;
        for (const auto &f : out.floating)
            (f.panel->id == "p1" ? z1 : z2) = f.z;
        QVERIFY(z1 > z2);
    }
    void reconcileIsIdempotent()
    {
        const auto snap = row({P("pa", "a"), P("pb", "b"), P("pc", "c")});
        const auto now = row({P("pa", "a"), P("pc", "c"), P("pn", "n")});
        const auto once = reconcile(snap, now);
        const auto twice = reconcile(once, now);
        QCOMPARE(leaves(twice), leaves(once));
    }
    void reconcileOnAnEmptyNowEmptiesTheLayout()
    {
        const auto snap = row({P("pa", "a")});
        const LayoutDocument now;
        const auto out = reconcile(snap, now);
        QVERIFY(viewIds(out).empty());
    }
};

QTEST_APPLESS_MAIN(TestHistory)
#include "test_history.moc"
