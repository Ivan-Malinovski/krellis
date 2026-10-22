#include "keynav.h"

#include "resize.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace krellis {

namespace {

constexpr double kEps = 1e-9;

double lo(const Rect &r, Axis a) { return a == Axis::X ? r.x : r.y; }
double hi(const Rect &r, Axis a) { return a == Axis::X ? r.x + r.w : r.y + r.h; }

Axis crossAxis(Axis a) { return a == Axis::X ? Axis::Y : Axis::X; }

Edge opposite(Edge e)
{
    switch (e) {
    case Edge::Left: return Edge::Right;
    case Edge::Right: return Edge::Left;
    case Edge::Top: return Edge::Bottom;
    default: return Edge::Top;
    }
}

// Where a panel stepping `dir` into `target` lands, as if it were dropped in the middle of that tile: the tile
// splits along its longer side (a square counts as wide). Entering along that side the panel takes the half it
// came through; entering across it, the half on its own side of the tile's middle (level with it counts as after).
Edge landingEdge(const Rect &origin, const Rect &target, Edge dir, const LayoutMetrics &metrics)
{
    const double w = target.w * (metrics.width > 0 ? metrics.width : 1), h = target.h * (metrics.height > 0 ? metrics.height : 1);
    const Axis along = w >= h ? Axis::X : Axis::Y;
    if (edgeAxis(dir) == along)
        return opposite(dir);
    const double mine = (lo(origin, along) + hi(origin, along)) / 2, middle = (lo(target, along) + hi(target, along)) / 2;
    if (along == Axis::X)
        return mine < middle - kEps ? Edge::Left : Edge::Right;
    return mine < middle - kEps ? Edge::Top : Edge::Bottom;
}

} // namespace

std::string neighbourInDirection(const Entries &entries, const std::string &from, Edge dir, bool skipVacant)
{
    const Entry *origin = entries.find(from);
    if (!origin)
        return {};
    const Axis axis = edgeAxis(dir), cross = crossAxis(axis);
    const bool before = edgeBefore(dir);
    const Rect &o = origin->rect;
    const double oCentre = (lo(o, axis) + hi(o, axis)) / 2, oCross = (lo(o, cross) + hi(o, cross)) / 2;

    std::string best;
    double bestOverlap = -1, bestGap = 0, bestCentre = 0;
    for (const auto &[id, entry] : entries) {
        if (id == from || entry.isVirtual || entry.node->kind != Node::Kind::Panel || (skipVacant && isVacant(entry.node)))
            continue;
        const Rect &r = entry.rect;
        // Wholly past the origin's edge on that side.
        const double gap = before ? lo(o, axis) - hi(r, axis) : lo(r, axis) - hi(o, axis);
        if (gap < -kEps)
            continue;
        const double overlap = std::min(hi(o, cross), hi(r, cross)) - std::max(lo(o, cross), lo(r, cross));
        const double centre = std::hypot((lo(r, axis) + hi(r, axis)) / 2 - oCentre, (lo(r, cross) + hi(r, cross)) / 2 - oCross);
        bool better;
        if (best.empty())
            better = true;
        else if (std::abs(overlap - bestOverlap) > kEps && (overlap > kEps || bestOverlap > kEps))
            better = overlap > bestOverlap; // sharing extent beats not sharing
        else if (std::abs(gap - bestGap) > kEps)
            better = gap < bestGap;
        else
            better = centre < bestCentre;
        if (better) {
            best = id;
            bestOverlap = overlap;
            bestGap = gap;
            bestCentre = centre;
        }
    }
    return best;
}

LayoutDocument moveInDirection(LayoutDocument doc, const std::string &panelId, Edge dir, const LayoutMetrics &metrics,
                               bool *moved)
{
    if (moved)
        *moved = false;
    if (!doc.root || !findNode(doc.root, panelId))
        return doc;
    const Entries entries = layoutRects(doc.root, &metrics);
    const Entry *self = entries.find(panelId);
    if (!self)
        return doc;
    const Axis axis = edgeAxis(dir);
    const bool before = edgeBefore(dir);
    const NodePtr panel = self->node;

    const std::string neighbour = neighbourInDirection(entries, panelId, dir, false);
    if (!neighbour.empty()) {
        // Direct siblings along the axis trade places and keep their slots' sizes.
        const NodePtr parent = parentSplit(doc.root, panelId);
        if (parent && parent->axis == axis) {
            const auto &kids = parent->children;
            const auto at = std::find_if(kids.begin(), kids.end(), [&](const NodePtr &c) { return c->id == panelId; });
            const long i = at - kids.begin(), j = i + (before ? -1 : 1);
            if (j >= 0 && j < long(kids.size()) && kids[size_t(j)]->id == neighbour) {
                auto order = kids;
                std::swap(order[size_t(i)], order[size_t(j)]);
                doc.root = replaceNode(doc.root, parent->id, makeSplit(parent->id, parent->axis, order, parent->weights));
                if (moved)
                    *moved = true;
                return doc;
            }
        }
        // Moving into an empty slot trades places with it rather than splitting a tile.
        if (isVacant(findNode(doc.root, neighbour))) {
            if (auto vacated = vacatePanel(doc, panelId)) {
                if (moved)
                    *moved = true;
                return fillVacancy(*vacated, neighbour, panel);
            }
        }
        const Edge landing = landingEdge(self->rect, entries.at(neighbour).rect, dir, metrics);
        doc = removePanel(std::move(doc), panelId, true);
        doc = insertPanel(std::move(doc), panel, DockTarget::beside(neighbour, landing));
        if (moved)
            *moved = true;
        return doc;
    }

    // Nothing that way: pop out to that edge of the whole layout, unless already flush with it.
    const Entry *root = entries.find(doc.root->id);
    if (!root)
        return doc;
    const bool flush = before ? lo(self->rect, axis) <= lo(root->rect, axis) + kEps
                              : hi(self->rect, axis) >= hi(root->rect, axis) - kEps;
    const bool alone = panelsOf(doc.root).size() < 2;
    const NodePtr parent = parentSplit(doc.root, panelId);
    // A panel that is flush with the edge inside a split along another axis can still pop out of that column.
    if (alone || (flush && (!parent || parent->axis == axis)))
        return doc;
    doc = removePanel(std::move(doc), panelId, true);
    if (!doc.root)
        return doc;
    doc = insertPanel(std::move(doc), panel, DockTarget::beside(doc.root->id, dir, 0.3));
    if (moved)
        *moved = true;
    return doc;
}

NodePtr resizeInDirection(const NodePtr &root, const LayoutMetrics &metrics, const std::string &panelId, Edge dir,
                          double fraction)
{
    if (!root)
        return nullptr;
    const Entries entries = layoutRects(root, &metrics);
    const Entry *self = entries.find(panelId);
    if (!self)
        return nullptr;
    const Axis axis = edgeAxis(dir);

    // The divider on `side` of the panel: the nearest ancestor split along the axis with a sibling that way.
    struct Divider {
        std::string split;
        int index = 0;
    };
    auto dividerOn = [&](Edge side) -> std::optional<Divider> {
        const bool towardsStart = edgeBefore(side);
        std::string id = panelId;
        for (const Entry *e = &entries.at(id); !e->parent.empty(); e = &entries.at(id)) {
            const std::string parentId = e->parent;
            const NodePtr &parent = entries.at(parentId).node;
            if (parent->kind == Node::Kind::Split && parent->axis == edgeAxis(side)) {
                const auto at = std::find_if(parent->children.begin(), parent->children.end(),
                                             [&](const NodePtr &c) { return c->id == id; });
                const int i = int(at - parent->children.begin());
                if (towardsStart && i > 0)
                    return Divider{parentId, i - 1};
                if (!towardsStart && i + 1 < int(parent->children.size()))
                    return Divider{parentId, i};
            }
            id = parentId;
        }
        return std::nullopt;
    };

    auto divider = dividerOn(dir);
    if (!divider) {
        const Edge opposite = axis == Axis::X ? (dir == Edge::Left ? Edge::Right : Edge::Left)
                                              : (dir == Edge::Top ? Edge::Bottom : Edge::Top);
        divider = dividerOn(opposite);
    }
    if (!divider)
        return nullptr;

    const auto result = dragBoundary(root, metrics, divider->split, divider->index);
    if (!result.valid)
        return nullptr;
    const double size = axis == Axis::X ? self->rect.w : self->rect.h;
    const double step = std::max(1e-4, size * fraction) * (edgeBefore(dir) ? -1 : 1);
    const double target = result.drag.start + step;
    const double clamped = std::max(result.drag.min, std::min(result.drag.max, target));
    if (std::abs(clamped - result.drag.start) < 1e-9)
        return nullptr;
    return result.drag.to(clamped);
}

} // namespace krellis
