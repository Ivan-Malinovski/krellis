#include "resize.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>

namespace trellis {

namespace {

constexpr double NEG_INF = -std::numeric_limits<double>::infinity();

struct Group {
    NodePtr node;
    std::vector<int> ids;
    bool cramped = false;
};

struct Edge1 {
    int other;
    double gap;
};

std::vector<int> topological(const std::vector<std::vector<Edge1>> &next)
{
    std::vector<int> incoming(next.size(), 0);
    for (const auto &edges : next)
        for (const auto &e : edges)
            incoming[e.other]++;
    std::deque<int> queue;
    for (size_t v = 0; v < incoming.size(); ++v)
        if (incoming[v] == 0)
            queue.push_back(int(v));
    std::vector<int> order;
    while (!queue.empty()) {
        const int u = queue.front();
        queue.pop_front();
        order.push_back(u);
        for (const auto &e : next[u])
            if (--incoming[e.other] == 0)
                queue.push_back(e.other);
    }
    return order;
}

NodePtr reweigh(const NodePtr &node, const std::map<std::string, std::vector<double>> &weights)
{
    if (node->kind == Node::Kind::Panel)
        return node;
    if (node->kind == Node::Kind::Stage) {
        if (!node->child)
            return node;
        NodePtr child = reweigh(node->child, weights);
        if (child == node->child)
            return node;
        auto copy = std::make_shared<Node>(*node);
        copy->child = child;
        return copy;
    }
    std::vector<NodePtr> children;
    bool same = true;
    for (size_t i = 0; i < node->children.size(); ++i) {
        children.push_back(reweigh(node->children[i], weights));
        same = same && children.back() == node->children[i];
    }
    auto it = weights.find(node->id);
    if (it == weights.end() && same)
        return node;
    auto copy = std::make_shared<Node>(*node);
    copy->children = children;
    if (it != weights.end())
        copy->weights = it->second;
    return copy;
}

} // namespace

BoundaryDragResult dragBoundary(const NodePtr &root, const LayoutMetrics &metrics, const std::string &splitId,
                                int index)
{
    auto entries = std::make_shared<Entries>(layoutRects(root, &metrics));
    const Entry *draggedEntry = entries->find(splitId);
    if (!draggedEntry || draggedEntry->node->kind != Node::Kind::Split || index < 0
        || index >= int(draggedEntry->node->children.size()) - 1)
        return {};
    const NodePtr dragged = draggedEntry->node;
    const Axis axis = dragged->axis;
    const double pixels = axis == Axis::X ? metrics.width : metrics.height;
    auto lo = [axis](const Rect &r) { return axis == Axis::X ? r.x : r.y; };
    auto len = [axis](const Rect &r) { return axis == Axis::X ? r.w : r.h; };
    auto scaleOf = [&](const std::string &id) {
        const Entry *e = entries->find(id);
        return e ? e->scale : 1.0;
    };

    auto pos = std::make_shared<std::vector<double>>();
    std::vector<std::vector<Edge1>> next, prev;
    auto position = [&](double at) {
        pos->push_back(at);
        next.emplace_back();
        prev.emplace_back();
        return int(pos->size()) - 1;
    };
    auto atLeast = [&](int from, int to, double gap) {
        next[from].push_back({to, gap});
        prev[to].push_back({from, gap});
    };
    auto minOf = [&](const Node &node) { return metrics.min(node, axis) * scaleOf(node.id) / pixels; };

    std::map<std::string, Group> groups;
    auto crampedAlong = [&](const NodePtr &node) {
        const double own = scaleOf(node->id);
        const double factor = scaleOf(node->children[0]->id) / own;
        if (factor >= 1 - 1e-9)
            return false;
        const double size = len(entries->at(node->id).rect) * pixels / own;
        double need = 0;
        for (const auto &c : node->children)
            need += metrics.min(*c, axis);
        return size / need <= factor + 1e-9;
    };
    std::function<void(const NodePtr &, int, int)> walk = [&](const NodePtr &node, int a, int b) {
        if (node->kind == Node::Kind::Panel)
            return atLeast(a, b, minOf(*node));
        if (node->kind == Node::Kind::Stage) {
            if (node->child)
                return walk(node->child, a, b);
            return atLeast(a, b, minOf(*node));
        }
        if (node->axis != axis) {
            for (const auto &c : node->children)
                walk(c, a, b);
            return;
        }
        std::vector<int> ids{a};
        for (size_t i = 1; i < node->children.size(); ++i)
            ids.push_back(position(lo(entries->at(node->children[i]->id).rect)));
        ids.push_back(b);
        groups[node->id] = Group{node, ids, crampedAlong(node)};
        for (size_t i = 0; i < node->children.size(); ++i)
            walk(node->children[i], ids[i], ids[i + 1]);
    };
    const Rect rootRect = entries->at(root->id).rect;
    const int first = position(lo(rootRect));
    const int last = position(lo(rootRect) + len(rootRect));
    walk(root, first, last);
    const int handle = groups.at(splitId).ids[index + 1];

    const auto order = topological(next);
    const size_t n = pos->size();
    std::vector<double> ahead(n, NEG_INF), behind(n, NEG_INF);
    ahead[handle] = 0;
    for (int u : order)
        if (ahead[u] > NEG_INF)
            for (const auto &e : next[u])
                ahead[e.other] = std::max(ahead[e.other], ahead[u] + e.gap);
    behind[handle] = 0;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const int u = *it;
        if (behind[u] > NEG_INF)
            for (const auto &e : prev[u])
                behind[e.other] = std::max(behind[e.other], behind[u] + e.gap);
    }

    const double start = (*pos)[handle];
    const double reachMax = std::max(start, (*pos)[last] - ahead[last]);
    const double reachMin = std::min(start, (*pos)[first] + behind[first]);

    auto solve = [=, groups = groups](double p) -> NodePtr {
        std::vector<double> moved(n);
        for (size_t v = 0; v < n; ++v) {
            const double at = (*pos)[v];
            if (p >= start)
                moved[v] = ahead[v] > NEG_INF ? std::max(at, p + ahead[v]) : at;
            else
                moved[v] = behind[v] > NEG_INF ? std::min(at, p - behind[v]) : at;
        }
        moved[handle] = p;
        std::map<std::string, std::vector<double>> weights;
        for (const auto &[id, g] : groups) {
            std::vector<double> before, after;
            for (size_t i = 1; i < g.ids.size(); ++i) {
                before.push_back((*pos)[g.ids[i]] - (*pos)[g.ids[i - 1]]);
                after.push_back(moved[g.ids[i]] - moved[g.ids[i - 1]]);
            }
            const double total = sum(after), was = sum(before);
            bool same = true;
            for (size_t i = 0; i < after.size(); ++i)
                same = same && std::abs(after[i] / total - before[i] / was) < 1e-12;
            if (same)
                continue;
            if (g.cramped) {
                bool innerStill = true;
                for (size_t i = 1; i + 1 < g.ids.size(); ++i)
                    innerStill = innerStill && moved[g.ids[i]] == (*pos)[g.ids[i]];
                if (innerStill)
                    continue;
            }
            std::vector<double> w;
            for (double a : after)
                w.push_back(a / total);
            weights[g.node->id] = w;
        }
        return weights.empty() ? root : reweigh(root, weights);
    };

    const std::string beside = dragged->children[index + 1]->id;
    auto landed = [=](const NodePtr &layout) {
        if (layout == root)
            return start;
        return lo(layoutRects(layout, &metrics).at(beside).rect);
    };
    const double minPos = reachMin == start ? start : std::min(start, landed(solve(reachMin)));
    const double maxPos = reachMax == start ? start : std::max(start, landed(solve(reachMax)));

    BoundaryDragResult result;
    result.valid = true;
    result.drag.axis = axis;
    result.drag.start = start;
    result.drag.min = minPos;
    result.drag.max = maxPos;
    result.drag.to = [=](double target) -> NodePtr {
        const double p = std::max(minPos, std::min(maxPos, target));
        NodePtr guess = solve(std::max(reachMin, std::min(reachMax, p)));
        if (std::abs(landed(guess) - p) < 1e-9)
            return guess;
        double below = p >= start ? start : reachMin;
        double above = p >= start ? reachMax : start;
        for (int i = 0; i < 50 && above - below > 1e-12; ++i) {
            const double mid = (below + above) / 2;
            if (landed(solve(mid)) < p)
                below = mid;
            else
                above = mid;
        }
        return solve(p >= start ? above : below);
    };
    return result;
}

} // namespace trellis
