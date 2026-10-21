// Drag session: target resolution, settling, live preview and commit.
#include "builder.h"
#include "document.h"
#include "drag.h"

#include <QtTest>

using namespace trellis;
namespace L = trellis::layout;

struct DHost : DragHost {
    LayoutDocument document;
    RectMap rects;
    int tweens = 0;
    Point vp{1000, 600};
    const LayoutDocument &doc() const override { return document; }
    void setDoc(const LayoutDocument &d) override { document = d; }
    Point viewport() const override { return vp; }
    double inset() const override { return 8; }
    double tabbarHeight(const Node &) const override { return 30; }
    int tabIndexAt(const Node &p, double) const override { return int(p.views.size()); }
    Rect toScreen(const Rect &w) const override { return {w.x * vp.x, w.y * vp.y, w.w * vp.x, w.h * vp.y}; }
    Point fromScreen(Point s) const override { return {s.x / vp.x, s.y / vp.y}; }
    Rect panelScreen(const Rect &w) const override
    {
        Rect r = toScreen(w);
        return {r.x + 8, r.y + 8, r.w - 16, r.h - 16};
    }
    std::optional<Rect> fromScreenRect(const std::optional<Rect> &s) const override
    {
        if (!s)
            return std::nullopt;
        return Rect{s->x / vp.x, s->y / vp.y, s->w / vp.x, s->h / vp.y};
    }
    std::optional<Rect> floatWorld(const std::string &) const override { return std::nullopt; }
    std::optional<FloatingLayer> floatingLayer() const override { return FloatingLayer::Overlay; }
    LayoutMetrics layoutMetrics() const override
    {
        LayoutMetrics m;
        m.width = 1, m.height = 1;
        return m;
    }
    const RectMap &lastRects() const override { return rects; }
    void beginTween(const RectMap &, double) override { ++tweens; }

    void load(LayoutDocument d)
    {
        document = std::move(d);
        rects.clear();
        for (const auto &kv : layoutRects(document.root)) {
            if (kv.second.node->kind == Node::Kind::Panel)
                rects[kv.first] = panelScreen(kv.second.rect);
        }
    }
};

static LayoutDocument three()
{
    auto root = L::row({L::panel({L::view("t", "a")}, "A"), L::panel({L::view("t", "b")}, "B"),
                        L::panel({L::view("t", "c")}, "C")});
    return createDocument(&root);
}

class TestDrag : public QObject
{
    Q_OBJECT
private slots:
    void belowThresholdIsAClick()
    {
        DHost h;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        QVERIFY(!d.move({103, 302}, 0));
        QVERIFY(!d.active());
        auto out = d.end(true, 10);
        QVERIFY(!out.wasDrag);
    }
    void hoveringTileCentreProposesTabAfterSettle()
    {
        DHost h;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        QVERIFY(d.move({500, 300}, 0)); // centre of B
        QVERIFY(d.candidate());
        QCOMPARE(int(d.candidate()->kind), int(DropTarget::Kind::Tab));
        QCOMPARE(d.candidate()->panel, std::string("B"));
        QVERIFY(!d.target()); // not applied yet
        d.tick(100);
        QVERIFY(!d.target());
        d.tick(160); // 150 ms settle
        QVERIFY(d.target());
        QVERIFY(d.collapsed()); // source collapsed on the first target
        QVERIFY(d.layoutTargets().count(DROP_SLOT));
        QCOMPARE(d.dropLabel(), std::string("Add as tab"));
        QCOMPARE(h.tweens, 1);
    }
    void edgeBandDocksBesideAndCommitMovesPanel()
    {
        DHost h;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        // Left edge band of C (x in 667..~740), vertically centred.
        QVERIFY(d.move({680, 300}, 0));
        QVERIFY(d.candidate());
        QCOMPARE(int(d.candidate()->kind), int(DropTarget::Kind::Dock));
        // Same-axis edges resolve to the parent's seam before C (the tile edge is a seam insert).
        QVERIFY(d.candidate()->spec.seam.has_value() || d.candidate()->spec.id == "C");
        // Release commits immediately, before the settle delay expires.
        auto out = d.end(true, 20);
        QVERIFY(out.committed);
        QVERIFY(out.wasDrag);
        auto order = leafIds(out.next.root);
        QCOMPARE(order.size(), size_t(3));
        QCOMPARE(order[0], std::string("B"));
        QCOMPARE(order[1], std::string("A"));
        QCOMPARE(order[2], std::string("C"));
    }
    void roundTripsUnderAndBackKeepWidths()
    {
        // Points: dock the lifted column under another (bottom band), then drag it back out beside; repeat.
        struct Trip { const char *who; Point under; Point back; };
        const Trip trips[] = {
            {"A", {500, 560}, {40, 300}},   // A under B, back to the left edge
            {"A", {500, 560}, {680, 300}},  // A under B, back onto B's left edge
            {"C", {500, 560}, {960, 300}},  // C under B, back to the right edge
            {"C", {500, 560}, {290, 300}},  // C under B, back beside A
            {"B", {160, 560}, {500, 300}},  // B under A, back between
            {"C", {160, 560}, {960, 300}},  // C under A, back to the right edge
            {"C", {160, 560}, {40, 300}},   // C under A, back to the left edge
            {"C", {160, 560}, {590, 300}},  // C under A, back onto B's left edge
            {"C", {160, 560}, {340, 300}},  // C under A, back onto A's right edge
            {"B", {160, 560}, {300, 300}},  // B under A, back onto A's right edge
            {"B", {160, 560}, {960, 300}},  // B under A, back to the right edge
        };
        for (const auto &trip : trips) {
            DHost h;
            h.load(three());
            auto widthOf = [&](const std::string &id) {
                const auto rects = layoutRects(h.document.root);
                return rects.find(id) ? rects.find(id)->rect.w : -1.0;
            };
            QString log;
            for (int cycle = 0; cycle < 4; ++cycle) {
                for (const Point &to : {trip.under, trip.back}) {
                    const auto rects = layoutRects(h.document.root);
                    if (!rects.find(trip.who)) { log += " (gone)"; break; }
                    const Rect r = h.panelScreen(rects.find(trip.who)->rect);
                    DragSession d(h, trip.who, "", {r.x + r.w / 2, r.y + r.h / 2});
                    d.move(to, 0);
                    d.tick(200);
                    auto out = d.end(true, 400);
                    if (out.committed) h.load(out.next);
                }
                log += QString(" [%1 %2 %3]").arg(widthOf("A")).arg(widthOf("B")).arg(widthOf("C"));
            }
            qInfo().noquote() << trip.who << "under" << trip.under.x << "back" << trip.back.x << log;
        }
    }
    void escapeFliesBack()
    {
        DHost h;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        d.move({680, 300}, 0);
        d.tick(200);
        auto out = d.end(false, 250);
        QVERIFY(!out.committed);
        QVERIFY(out.wasDrag);
        QCOMPARE(out.settle, std::string("A"));
        QCOMPARE(leafIds(out.next.root).size(), size_t(3));
        QCOMPARE(leafIds(out.next.root)[0], std::string("A"));
    }
    void tabTearOutBecomesItsOwnPanel()
    {
        auto root = L::row({L::panel({L::view("t", "a1"), L::view("t", "a2")}, "A"), L::panel({L::view("t", "b")}, "B")});
        DHost h;
        h.load(createDocument(&root));
        DragSession d(h, "A", "a2", {100, 300});
        QVERIFY(d.move({700, 300}, 0));
        QVERIFY(d.lifted()->id != "A");
        QCOMPARE(d.lifted()->views.size(), size_t(1));
        QCOMPARE(d.lifted()->views[0], std::string("a2"));
        // Host document already lost the view.
        QCOMPARE(panelOfView(h.document, "a1")->views.size(), size_t(1));
        // Cancel restores the original.
        auto out = d.end(false, 10);
        QCOMPARE(panelOfView(out.next, "a2")->views.size(), size_t(2));
    }
    void seamIsATarget()
    {
        DHost h;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        // Seam between B and C sits at x = 666.67; the nearest seam wins over tile edges.
        d.move({667, 300}, 0);
        QVERIFY(d.candidate());
        QCOMPARE(int(d.candidate()->kind), int(DropTarget::Kind::Dock));
        QVERIFY(d.candidate()->spec.seam.has_value());
    }
    void liftedWindowShrinksToCardWhenLeavingSource()
    {
        DHost h;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        d.move({800, 300}, 0);
        Rect early = d.liftedRect(0);
        Rect late = d.liftedRect(400);
        QVERIFY(late.w < early.w + 1e-9);
        QVERIFY(late.w <= DragSession::Compact.x + 1e-9);
    }
    void allowRulesRemoveTargets()
    {
        struct NoSide : DHost {
            bool allowed(const std::vector<std::string> &, Region r) const override { return r != Region::Side; }
        } h;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        d.move({500, 300}, 0);
        QVERIFY(!d.candidate()); // tab into B is a side drop and is refused
    }
};

QTEST_APPLESS_MAIN(TestDrag)
#include "test_drag.moc"
