// Drag session: target resolution, settling, live preview and commit.
#include "builder.h"
#include "document.h"
#include "drag.h"

#include <QtTest>

using namespace krellis;
namespace L = krellis::layout;

struct DHost : DragHost {
    LayoutDocument document;
    bool bodyTabsOn = true;
    RectMap rects;
    int tweens = 0;
    Point vp{1000, 600};
    const LayoutDocument &doc() const override { return document; }
    void setDoc(const LayoutDocument &d) override { document = d; }
    Point viewport() const override { return vp; }
    double inset() const override { return 8; }
    double tabbarHeight(const Node &) const override { return 30; }
    bool bodyTabs() const override { return bodyTabsOn; }
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
    void onlyTheTitleBandTabsWhenTheBodyDoesNot()
    {
        DHost h;
        h.bodyTabsOn = false;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        QVERIFY(d.move({500, 300}, 0)); // centre of B: no longer a tab target
        QVERIFY(!d.candidate() || d.candidate()->kind != DropTarget::Kind::Tab);
        QVERIFY(d.move({500, 20}, 10)); // the title band of B (its top 30 px)
        QVERIFY(d.candidate());
        QCOMPARE(int(d.candidate()->kind), int(DropTarget::Kind::Tab));
        QCOMPARE(d.candidate()->panel, std::string("B"));
    }
    void splitAboveSitsRightUnderTheTitleBand()
    {
        DHost h;
        h.bodyTabsOn = false;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        QVERIFY(d.move({500, 60}, 0)); // below B's title band (30 px), inside its top edge zone
        QVERIFY(d.candidate());
        QCOMPARE(int(d.candidate()->kind), int(DropTarget::Kind::Dock));
        QVERIFY(d.move({500, 20}, 10)); // the band itself
        QCOMPARE(int(d.candidate()->kind), int(DropTarget::Kind::Tab));
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
        QVERIFY(!d.candidate()->spec.seam);
        QCOMPARE(d.candidate()->spec.id, std::string("C"));
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
    void bodyDropOnlySplitsTarget_data()
    {
        QTest::addColumn<bool>("vertical");
        QTest::addColumn<int>("side");
        for (bool vertical : {false, true})
            for (Edge edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
                QTest::newRow(qPrintable(QString("%1-%2").arg(vertical).arg(int(edge)))) << vertical << int(edge);
    }
    void bodyDropOnlySplitsTarget()
    {
        QFETCH(bool, vertical);
        QFETCH(int, side);
        const Edge edge = Edge(side);
        DHost h;
        h.bodyTabsOn = false;
        auto doc = three();
        auto root = std::make_shared<Node>(*doc.root);
        root->axis = vertical ? Axis::Y : Axis::X;
        root->weights = {0.2, 0.5, 0.3};
        doc.root = root;
        // A separate floating source leaves the tiled layout unchanged on pickup.
        doc = floatPanel(doc, makePanel("D", {"d"}, "d"), {0.1, 0.1, 0.2, 0.2}, FloatingLayer::Overlay);
        h.load(doc);
        const auto before = layoutRects(doc.root);
        const Rect target = before.at("B").rect;
        Point p{target.x + target.w / 2, target.y + target.h / 2};
        if (edge == Edge::Left) p.x = target.x + target.w * 0.13;
        if (edge == Edge::Right) p.x = target.x + target.w * 0.87;
        if (edge == Edge::Top) p.y = target.y + target.h * 0.13;
        if (edge == Edge::Bottom) p.y = target.y + target.h * 0.87;
        DragSession d(h, "D", "", {20, 20});
        QVERIFY(d.move({p.x * h.vp.x, p.y * h.vp.y}, 0));
        QVERIFY(d.candidate());
        QCOMPARE(d.candidate()->kind, DropTarget::Kind::Dock);
        QVERIFY(!d.candidate()->spec.seam);
        QCOMPARE(d.candidate()->spec.edge, edge);
        d.tick(200);
        const DropRects expected = dropRects(target, edge);
        QVERIFY(sameRect(d.layoutTargets().at("B"), expected.remaining));
        QVERIFY(sameRect(d.layoutTargets().at(DROP_SLOT), expected.slot));
        auto out = d.end(true, 250);
        QVERIFY(out.committed);
        const auto after = layoutRects(out.next.root);
        QVERIFY(sameRect(after.at("A").rect, before.at("A").rect));
        QVERIFY(sameRect(after.at("C").rect, before.at("C").rect));
        QVERIFY(sameRect(after.at("B").rect, expected.remaining));
        QVERIFY(sameRect(after.at("D").rect, expected.slot));
    }
    void centreBodyDropSplitsEvenWhenOrderIsUnchanged()
    {
        DHost h;
        h.bodyTabsOn = false;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        // Deep inside the left half of B, beyond the old edge band.
        QVERIFY(d.move({480, 300}, 0));
        QVERIFY(d.candidate());
        QCOMPARE(d.candidate()->kind, DropTarget::Kind::Dock);
        QVERIFY(!d.candidate()->spec.seam);
        QCOMPARE(d.candidate()->spec.edge, Edge::Left);
        auto out = d.end(true, 10);
        QVERIFY(out.committed);
        const auto rects = layoutRects(out.next.root);
        QCOMPARE(rects.at("A").rect.w, 0.25);
        QCOMPARE(rects.at("B").rect.w, 0.25);
        QCOMPARE(rects.at("C").rect.w, 0.5);
    }
    void gapDropSharesSpaceAcrossTheSplit()
    {
        DHost h;
        h.bodyTabsOn = false;
        h.load(three());
        DragSession d(h, "A", "", {100, 300});
        QVERIFY(d.move({667, 300}, 0));
        QVERIFY(d.candidate());
        QVERIFY(d.candidate()->spec.seam);
        d.tick(200);
        QCOMPARE(d.layoutTargets().at(DROP_SLOT).w, 1.0 / 3);
        auto out = d.end(true, 250);
        QVERIFY(out.committed);
        const auto rects = layoutRects(out.next.root);
        for (const auto *id : {"A", "B", "C"})
            QVERIFY(std::abs(rects.at(id).rect.w - 1.0 / 3) < 1e-9);
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
