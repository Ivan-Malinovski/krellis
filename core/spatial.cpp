#include "spatial.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>

namespace krellis {

std::vector<std::string> leafIds(const NodePtr &node)
{
    if (!node)
        return {};
    if (node->kind == Node::Kind::Panel)
        return {node->id};
    if (node->kind == Node::Kind::Stage)
        return node->child ? leafIds(node->child) : std::vector<std::string>{node->id};
    std::vector<std::string> out;
    for (const auto &c : node->children) {
        auto l = leafIds(c);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}

std::string rangeId(const std::string &splitId, const std::vector<std::string> &children)
{
    // JSON array of [splitId, ...children], as the reference does, so ids stay comparable.
    std::string out = "range:[\"" + splitId + "\"";
    for (const auto &c : children)
        out += ",\"" + c + "\"";
    return out + "]";
}

Entries focusLayout(const NodePtr &root, const LayoutMetrics *metrics)
{
    Entries entries = layoutRects(root, metrics);
    std::vector<NodePtr> nodes;
    for (const auto &kv : entries)
        nodes.push_back(kv.second.node);
    for (const auto &node : nodes) {
        if (node->kind != Node::Kind::Split || node->children.size() < 3)
            continue;
        const size_t n = node->children.size();
        for (size_t start = 0; start + 1 < n; ++start) {
            for (size_t end = start + 2; end <= n; ++end) {
                if (start == 0 && end == n)
                    continue;
                std::vector<NodePtr> children(node->children.begin() + start, node->children.begin() + end);
                const Rect first = entries.at(children.front()->id).rect;
                const Rect last = entries.at(children.back()->id).rect;
                std::vector<std::string> ids;
                for (const auto &c : children)
                    ids.push_back(c->id);
                const std::string id = rangeId(node->id, ids);
                std::vector<double> weights(node->weights.begin() + start, node->weights.begin() + end);
                const double total = sum(weights);
                for (auto &w : weights)
                    w /= total;
                auto virt = makeSplit(id, node->axis, children, weights);
                entries.set(id, Entry{virt, {first.x, first.y, last.x + last.w - first.x, last.y + last.h - first.y},
                                      node->id, 1.0, true});
            }
        }
    }
    return entries;
}

bool sameRect(const Rect &a, const Rect &b, double eps)
{
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.w - b.w) < eps
        && std::abs(a.h - b.h) < eps;
}

std::string navParent(const Entries &entries, const std::string &id)
{
    const Entry *entry = entries.find(id);
    if (!entry)
        return {};
    std::string at = entry->parent;
    while (!at.empty()) {
        const Entry *parent = entries.find(at);
        if (!parent)
            return {};
        if (!sameRect(parent->rect, entry->rect))
            return at;
        at = parent->parent;
    }
    return {};
}

std::string navNode(const Entries &entries, const std::string &id)
{
    const Entry *e = entries.find(id);
    NodePtr node = e ? e->node : nullptr;
    while (node && node->kind == Node::Kind::Stage && node->child)
        node = node->child;
    return node ? node->id : id;
}

bool containsNode(const NodePtr &node, const std::string &id)
{
    if (node->id == id)
        return true;
    if (node->kind == Node::Kind::Stage)
        return node->child && containsNode(node->child, id);
    if (node->kind == Node::Kind::Split)
        for (const auto &c : node->children)
            if (containsNode(c, id))
                return true;
    return false;
}

std::string bestFit(const Rect &camera, const Entries &entries)
{
    std::string best;
    double score = std::numeric_limits<double>::infinity();
    for (const auto &[id, e] : entries) {
        if (e.node->kind == Node::Kind::Stage && e.node->child)
            continue;
        const Rect &r = e.rect;
        const double size = std::abs(std::log(r.w / camera.w)) + std::abs(std::log(r.h / camera.h));
        const double distance = std::hypot((r.x + r.w / 2 - camera.x - camera.w / 2) / camera.w,
                                           (r.y + r.h / 2 - camera.y - camera.h / 2) / camera.h);
        const double next = size + distance * 2.5;
        if (next < score) {
            best = id;
            score = next;
        }
    }
    return best;
}

std::string hierarchyStep(const Entries &entries, const std::string &focused, Direction dir, Point point)
{
    const std::string id = navNode(entries, focused);
    const Entry *entry = entries.find(id);
    if (!entry)
        return focused;
    if (dir == Direction::Out) {
        const std::string parent = navParent(entries, id);
        return parent.empty() ? id : parent;
    }
    if (entry->node->kind != Node::Kind::Split)
        return id;
    const auto &split = entry->node;
    const double coordinate = split->axis == Axis::X ? point.x : point.y;
    for (size_t i = 0; i + 1 < split->children.size(); ++i) {
        const Rect &r = entries.at(split->children[i]->id).rect;
        if (coordinate < (split->axis == Axis::X ? r.x + r.w : r.y + r.h))
            return navNode(entries, split->children[i]->id);
    }
    return navNode(entries, split->children.back()->id);
}

std::string frameLeaves(const NodePtr &root, const std::vector<std::string> &ids)
{
    if (!root)
        return {};
    const Entries entries = focusLayout(root);
    std::vector<std::string> wanted;
    for (const auto &id : ids)
        if (entries.has(id))
            wanted.push_back(id);
    const Entry *best = &entries.at(root->id);
    for (const auto &[key, entry] : entries) {
        if (entry.node->kind == Node::Kind::Stage && entry.node->child)
            continue;
        if (entry.rect.w * entry.rect.h < best->rect.w * best->rect.h
            && std::all_of(wanted.begin(), wanted.end(),
                           [&](const std::string &id) { return containsNode(entry.node, id); }))
            best = &entry;
    }
    return best->node->id;
}

Rect rectangleCamera(Point a, Point b, const Rect &viewport, const Rect &camera)
{
    auto clamp = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    const double ax = clamp((a.x - viewport.x) / viewport.w);
    const double ay = clamp((a.y - viewport.y) / viewport.h);
    const double bx = clamp((b.x - viewport.x) / viewport.w);
    const double by = clamp((b.y - viewport.y) / viewport.h);
    return {camera.x + std::min(ax, bx) * camera.w, camera.y + std::min(ay, by) * camera.h,
            std::abs(bx - ax) * camera.w, std::abs(by - ay) * camera.h};
}

VisitHistory recordVisit(const std::vector<Visit> &visits, int index, const Visit &visit)
{
    if (index >= 0 && index < int(visits.size())) {
        const Visit &previous = visits[index];
        if (previous.id == visit.id && previous.leaves == visit.leaves)
            return {visits, index};
    }
    std::vector<Visit> next(visits.begin(), visits.begin() + std::min<size_t>(index + 1, visits.size()));
    next.push_back(visit);
    return {next, int(next.size()) - 1};
}

std::string savedFrameDestination(const NodePtr &root, const std::vector<std::string> &leaves)
{
    const Entries entries = layoutRects(root);
    std::vector<std::string> surviving;
    for (const auto &id : leaves)
        if (entries.has(id))
            surviving.push_back(id);
    return surviving.empty() ? std::string() : frameLeaves(root, surviving);
}

MaximizeResult maximizeTransition(const Entries &entries, const std::string &rootId, const std::string &focused,
                                  const std::string &id, const std::optional<MaximizeSession> &session)
{
    if (focused == id) {
        std::string destination;
        if (session && session->id == id) {
            destination = rootId;
            for (const auto &key : session->returnPath)
                if (key != id && entries.has(key)) {
                    destination = key;
                    break;
                }
        } else {
            destination = navParent(entries, id);
            if (destination.empty())
                destination = rootId;
        }
        return {destination, std::nullopt};
    }
    MaximizeSession next{id, {}};
    std::string at = focused;
    while (!at.empty()) {
        next.returnPath.push_back(at);
        const Entry *e = entries.find(at);
        at = e ? e->parent : std::string();
    }
    return {id, next};
}

std::optional<Edge> dropEdge(Point point, const Rect &rect, double band)
{
    if (rect.w <= 0 || rect.h <= 0)
        return std::nullopt;
    const double x = (point.x - rect.x) / rect.w;
    const double y = (point.y - rect.y) / rect.h;
    if (x < 0 || x > 1 || y < 0 || y > 1)
        return std::nullopt;
    std::pair<Edge, double> edges[] = {{Edge::Left, x}, {Edge::Right, 1 - x}, {Edge::Top, y}, {Edge::Bottom, 1 - y}};
    std::stable_sort(std::begin(edges), std::end(edges), [](auto &a, auto &b) { return a.second < b.second; });
    if (edges[0].second <= band)
        return edges[0].first;
    return std::nullopt;
}

Edge pointerSpawnEdge(Point point, const Rect &rect, double widthPx, double heightPx)
{
    if (const auto edge = dropEdge(point, rect))
        return *edge;
    if (rect.w * widthPx >= rect.h * heightPx)
        return point.x < rect.x + rect.w / 2 ? Edge::Left : Edge::Right;
    return point.y < rect.y + rect.h / 2 ? Edge::Top : Edge::Bottom;
}

DropRects dropRects(const Rect &rect, Edge edge)
{
    Rect slot = rect, remaining = rect, collapsed = rect;
    if (edge == Edge::Left || edge == Edge::Right) {
        slot.w = remaining.w = rect.w / 2;
        collapsed.w = 0;
        if (edge == Edge::Left) {
            remaining.x += slot.w;
        } else {
            slot.x += slot.w;
            collapsed.x += rect.w;
        }
    } else {
        slot.h = remaining.h = rect.h / 2;
        collapsed.h = 0;
        if (edge == Edge::Top) {
            remaining.y += slot.h;
        } else {
            slot.y += slot.h;
            collapsed.y += rect.h;
        }
    }
    return {slot, remaining, collapsed};
}

DockTargetSpec seamTarget(const NodePtr &group, int boundary)
{
    DockTargetSpec t;
    t.id = group->id;
    t.edge = group->axis == Axis::X ? Edge::Left : Edge::Top;
    t.boundary = boundary;
    SplitSeam seam;
    seam.axis = group->axis;
    seam.leaves = leafIds(group);
    for (int i = 0; i < boundary; ++i) {
        auto l = leafIds(group->children[i]);
        seam.before.insert(seam.before.end(), l.begin(), l.end());
    }
    t.seam = seam;
    return t;
}

DockTargetSpec tileDropTarget(const Entries &entries, const std::string &id, Edge edge)
{
    const Entry *entry = entries.find(id);
    if (entry && !entry->parent.empty()) {
        if (const Entry *parent = entries.find(entry->parent);
            parent && parent->node->kind == Node::Kind::Split && parent->node->axis == edgeAxis(edge)) {
            int index = 0;
            for (size_t i = 0; i < parent->node->children.size(); ++i)
                if (parent->node->children[i]->id == id)
                    index = int(i);
            return seamTarget(parent->node, index + ((edge == Edge::Right || edge == Edge::Bottom) ? 1 : 0));
        }
    }
    DockTargetSpec t;
    t.id = id;
    t.edge = edge;
    return t;
}

std::optional<DockTargetSpec> frameDropTarget(const NodePtr &group, Point point, double width, double height,
                                              double radius)
{
    if (point.x < 0 || point.y < 0 || point.x > width || point.y > height)
        return std::nullopt;
    std::pair<Edge, double> d[] = {{Edge::Left, point.x},
                                   {Edge::Right, width - point.x},
                                   {Edge::Top, point.y},
                                   {Edge::Bottom, height - point.y}};
    std::stable_sort(std::begin(d), std::end(d), [](auto &a, auto &b) { return a.second < b.second; });
    const auto [edge, distance] = d[0];
    if (distance > radius)
        return std::nullopt;
    const Axis axis = edgeAxis(edge);
    const bool after = edge == Edge::Right || edge == Edge::Bottom;
    const NodePtr inner = (group->kind == Node::Kind::Stage && group->child) ? group->child : group;
    if (inner->kind == Node::Kind::Split && inner->axis == axis)
        return seamTarget(inner, after ? int(inner->children.size()) : 0);
    DockTargetSpec t;
    t.id = group->id;
    t.edge = edge;
    SplitSeam seam;
    seam.axis = axis;
    seam.leaves = leafIds(group);
    if (after)
        seam.before = seam.leaves;
    t.seam = seam;
    return t;
}

NodePtr insertAtSeam(const NodePtr &root, const SplitSeam &seam, const NodePtr &incoming, const std::string &splitId)
{
    std::set<std::string> surviving;
    for (const auto &id : leafIds(root))
        if (std::find(seam.leaves.begin(), seam.leaves.end(), id) != seam.leaves.end())
            surviving.insert(id);
    if (surviving.empty())
        return root;
    auto holdsAll = [&](const NodePtr &n) {
        const auto ids = leafIds(n);
        const std::set<std::string> set(ids.begin(), ids.end());
        return std::all_of(surviving.begin(), surviving.end(), [&](const std::string &s) { return set.count(s); });
    };
    std::function<NodePtr(const NodePtr &)> insert = [&](const NodePtr &node) -> NodePtr {
        if (node->kind == Node::Kind::Stage && node->child) {
            if (holdsAll(node->child) && !surviving.count(node->id)) {
                auto stage = std::make_shared<Node>(*node);
                stage->child = insert(node->child);
                return stage;
            }
        }
        if (node->kind == Node::Kind::Split) {
            int container = -1;
            for (size_t i = 0; i < node->children.size(); ++i)
                if (holdsAll(node->children[i])) {
                    container = int(i);
                    break;
                }
            if (container >= 0) {
                auto copy = std::make_shared<Node>(*node);
                copy->children[container] = insert(node->children[container]);
                return copy;
            }
            if (node->axis == seam.axis) {
                int index = -1;
                for (size_t i = 0; i < node->children.size() && index < 0; ++i)
                    for (const auto &id : leafIds(node->children[i]))
                        if (std::find(seam.before.begin(), seam.before.end(), id) == seam.before.end()) {
                            index = int(i);
                            break;
                        }
                auto children = node->children;
                const double total = sum(node->weights);
                const double share = 1.0 / double(children.size() + 1);
                std::vector<double> weights;
                for (double w : node->weights)
                    weights.push_back(w / total * (1 - share));
                const int at = index < 0 ? int(children.size()) : index;
                children.insert(children.begin() + at, incoming);
                weights.insert(weights.begin() + at, share);
                auto copy = std::make_shared<Node>(*node);
                copy->children = children;
                copy->weights = weights;
                return copy;
            }
        }
        bool before = true;
        for (const auto &id : surviving)
            if (std::find(seam.before.begin(), seam.before.end(), id) != seam.before.end())
                before = false;
        Edge edge;
        if (seam.axis == Axis::X)
            edge = before ? Edge::Left : Edge::Right;
        else
            edge = before ? Edge::Top : Edge::Bottom;
        return insertBeside(node, node->id, incoming, edge, splitId);
    };
    NodePtr result = normalize(insert(root));
    return result ? result : root;
}

NodePtr applyDockTarget(const NodePtr &root, const DockTargetSpec &target, const NodePtr &incoming,
                        const std::string &splitId)
{
    if (!root)
        return incoming;
    if (target.seam)
        return insertAtSeam(root, *target.seam, incoming, splitId);
    return insertBeside(root, target.id, incoming, target.edge, splitId, 0.5, target.nested);
}

} // namespace krellis
