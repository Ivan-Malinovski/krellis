#include "tree.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace krellis {

NodePtr makePanel(std::string id, std::vector<std::string> views, std::string selected)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Panel;
    n->id = std::move(id);
    if (selected.empty() && !views.empty())
        selected = views.front();
    n->views = std::move(views);
    n->selected = std::move(selected);
    return n;
}

NodePtr makeVacancy(std::string id)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Panel;
    n->id = std::move(id);
    n->vacant = true;
    return n;
}

NodePtr makeSplit(std::string id, Axis axis, std::vector<NodePtr> children, std::vector<double> weights)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Split;
    n->id = std::move(id);
    n->axis = axis;
    if (weights.size() != children.size())
        weights.assign(children.size(), 1.0);
    n->children = std::move(children);
    n->weights = std::move(weights);
    return n;
}

NodePtr makeStage(std::string id, NodePtr child)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Stage;
    n->id = std::move(id);
    n->child = std::move(child);
    return n;
}

double sum(const std::vector<double> &values)
{
    double s = 0;
    for (double v : values)
        s += v;
    return s;
}

namespace {

Rect slice(Rect rect, Axis axis, double offset, double share)
{
    if (axis == Axis::X) {
        rect.x += offset * rect.w;
        rect.w *= share;
    } else {
        rect.y += offset * rect.h;
        rect.h *= share;
    }
    return rect;
}

void layoutByWeights(const NodePtr &node, Rect rect, const std::string &parent, Entries &out)
{
    if (!node)
        return;
    out.set(node->id, Entry{node, rect, parent, 1.0, false});
    if (node->kind == Node::Kind::Stage) {
        layoutByWeights(node->child, rect, node->id, out);
    } else if (node->kind == Node::Kind::Split) {
        const double total = sum(node->weights);
        double offset = 0;
        for (size_t i = 0; i < node->children.size(); ++i) {
            const double share = node->weights[i] / total;
            layoutByWeights(node->children[i], slice(rect, node->axis, offset, share), node->id, out);
            offset += share;
        }
    }
}

struct Size {
    double w, h;
};

void layoutWithMinimums(const NodePtr &node, Rect rect, Size size, double scale, const std::string &parent,
                        const LayoutMetrics &metrics, Entries &out)
{
    if (!node)
        return;
    if (node->kind == Node::Kind::Stage) {
        out.set(node->id, Entry{node, rect, parent, scale, false});
        layoutWithMinimums(node->child, rect, size, scale, node->id, metrics, out);
        return;
    }
    if (node->kind != Node::Kind::Split) {
        out.set(node->id, Entry{node, rect, parent, scale, false});
        return;
    }
    const bool alongX = node->axis == Axis::X;
    const Axis crossAxis = alongX ? Axis::Y : Axis::X;
    std::vector<double> mins;
    for (const auto &c : node->children)
        mins.push_back(metrics.min(*c, node->axis));
    const double needAlong = sum(mins);
    double needAcross = 0;
    for (const auto &c : node->children)
        needAcross = std::max(needAcross, metrics.min(*c, crossAxis));
    const double sizeAlong = alongX ? size.w : size.h;
    const double sizeAcross = alongX ? size.h : size.w;
    const double s = std::min({1.0, needAlong > 0 ? sizeAlong / needAlong : 1.0,
                               needAcross > 0 ? sizeAcross / needAcross : 1.0});
    const Size natural{size.w / s, size.h / s};
    const double naturalAlong = alongX ? natural.w : natural.h;
    const std::vector<double> extents = distribute(naturalAlong, node->weights, mins);
    std::vector<double> fractions;
    for (double e : extents)
        fractions.push_back(e / naturalAlong);

    auto adjusted = std::make_shared<Node>(*node);
    adjusted->weights = fractions;
    out.set(node->id, Entry{adjusted, rect, parent, scale, false});
    double offset = 0;
    for (size_t i = 0; i < node->children.size(); ++i) {
        const Size childSize = alongX ? Size{extents[i], natural.h} : Size{natural.w, extents[i]};
        layoutWithMinimums(node->children[i], slice(rect, node->axis, offset, fractions[i]), childSize, scale * s,
                           node->id, metrics, out);
        offset += fractions[i];
    }
}

NodePtr withChild(const Node &stage, NodePtr child)
{
    auto n = std::make_shared<Node>(stage);
    n->child = std::move(child);
    return n;
}

NodePtr withSplit(const Node &split, std::vector<NodePtr> children, std::vector<double> weights)
{
    auto n = std::make_shared<Node>(split);
    n->children = std::move(children);
    n->weights = std::move(weights);
    return n;
}

} // namespace

Entries layoutRects(const NodePtr &root, const LayoutMetrics *metrics)
{
    Entries out;
    if (!metrics || !metrics->min)
        layoutByWeights(root, UNIT, {}, out);
    else
        layoutWithMinimums(root, UNIT, {metrics->width, metrics->height}, 1.0, {}, *metrics, out);
    return out;
}

std::vector<double> distribute(double total, const std::vector<double> &weights, const std::vector<double> &mins)
{
    std::set<size_t> fixed;
    std::vector<double> extents(weights.size(), 0.0);
    for (;;) {
        std::vector<size_t> free;
        for (size_t i = 0; i < weights.size(); ++i)
            if (!fixed.count(i))
                free.push_back(i);
        double fixedSum = 0;
        for (size_t i : fixed)
            fixedSum += mins[i];
        const double left = total - fixedSum;
        double weight = 0;
        for (size_t i : free)
            weight += weights[i];
        for (size_t i = 0; i < weights.size(); ++i) {
            if (fixed.count(i))
                extents[i] = mins[i];
            else
                extents[i] = weight > 0 ? left * weights[i] / weight : left / double(free.size());
        }
        bool any = false;
        for (size_t i : free) {
            if (extents[i] < mins[i] - 1e-9) {
                fixed.insert(i);
                any = true;
            }
        }
        if (!any)
            return extents;
    }
}

NodePtr findNode(const NodePtr &node, const std::string &id)
{
    if (!node)
        return nullptr;
    if (node->id == id)
        return node;
    if (node->kind == Node::Kind::Stage)
        return findNode(node->child, id);
    if (node->kind == Node::Kind::Split)
        for (const auto &c : node->children)
            if (auto f = findNode(c, id))
                return f;
    return nullptr;
}

std::vector<NodePtr> panelsOf(const NodePtr &node)
{
    if (!node)
        return {};
    if (node->kind == Node::Kind::Panel)
        return {node};
    if (node->kind == Node::Kind::Stage)
        return panelsOf(node->child);
    std::vector<NodePtr> out;
    for (const auto &c : node->children) {
        auto p = panelsOf(c);
        out.insert(out.end(), p.begin(), p.end());
    }
    return out;
}

NodePtr findStage(const NodePtr &node)
{
    if (!node)
        return nullptr;
    if (node->kind == Node::Kind::Stage)
        return node;
    if (node->kind == Node::Kind::Split)
        for (const auto &c : node->children)
            if (auto s = findStage(c))
                return s;
    return nullptr;
}

bool contains(const NodePtr &root, const std::string &ancestor, const std::string &id)
{
    auto node = findNode(root, ancestor);
    return node && findNode(node, id);
}

NodePtr normalize(const NodePtr &node)
{
    if (node->kind == Node::Kind::Panel)
        return node->views.empty() && !node->vacant ? nullptr : node;
    if (node->kind == Node::Kind::Stage) {
        NodePtr child = node->child ? normalize(node->child) : nullptr;
        if (child == node->child)
            return node;
        return withChild(*node, child);
    }
    std::vector<NodePtr> children;
    std::vector<double> weights;
    for (size_t i = 0; i < node->children.size(); ++i) {
        NodePtr next = normalize(node->children[i]);
        const double raw = node->weights[i];
        const double weight = (std::isfinite(raw) && raw > 0) ? raw : 1e-3;
        if (!next)
            continue;
        if (next->kind == Node::Kind::Split && next->axis == node->axis) {
            const double total = sum(next->weights);
            for (size_t j = 0; j < next->children.size(); ++j) {
                children.push_back(next->children[j]);
                weights.push_back(weight * next->weights[j] / total);
            }
        } else {
            children.push_back(next);
            weights.push_back(weight);
        }
    }
    if (children.empty())
        return nullptr;
    if (children.size() == 1)
        return children[0];
    const double total = sum(weights);
    std::vector<double> normalized;
    for (double w : weights)
        normalized.push_back(w / total);
    bool same = children.size() == node->children.size();
    for (size_t i = 0; same && i < children.size(); ++i)
        same = children[i] == node->children[i] && std::abs(normalized[i] - node->weights[i]) < 1e-12;
    if (same)
        return node;
    return withSplit(*node, children, normalized);
}

NodePtr removeNode(const NodePtr &root, const std::string &id, bool keepProportions)
{
    if (!root)
        return nullptr;
    if (root->id == id)
        return root->kind == Node::Kind::Stage ? root : nullptr;
    if (root->kind == Node::Kind::Panel)
        return root;
    if (root->kind == Node::Kind::Stage) {
        if (!root->child)
            return root;
        if (root->child->id == id)
            return withChild(*root, nullptr);
        NodePtr child = removeNode(root->child, id, keepProportions);
        return child == root->child ? root : withChild(*root, child);
    }
    int index = -1;
    for (size_t i = 0; i < root->children.size(); ++i)
        if (root->children[i]->id == id) {
            index = int(i);
            break;
        }
    if (index >= 0) {
        if (root->children[index]->kind == Node::Kind::Stage)
            return root;
        auto children = root->children;
        auto weights = root->weights;
        const double removed = weights[index];
        children.erase(children.begin() + index);
        weights.erase(weights.begin() + index);
        if (children.empty())
            return nullptr;
        if (!keepProportions)
            weights[std::min<size_t>(index, weights.size() - 1)] += removed;
        return normalize(withSplit(*root, children, weights));
    }
    bool changed = false;
    std::vector<NodePtr> children;
    std::vector<double> weights;
    for (size_t i = 0; i < root->children.size(); ++i) {
        NodePtr next = removeNode(root->children[i], id, keepProportions);
        if (next != root->children[i])
            changed = true;
        if (next) {
            children.push_back(next);
            weights.push_back(root->weights[i]);
        } else if (!weights.empty() && !keepProportions) {
            weights.back() += root->weights[i];
        }
    }
    if (!changed)
        return root;
    if (children.empty())
        return nullptr;
    return normalize(withSplit(*root, children, weights));
}

NodePtr insertBeside(const NodePtr &root, const std::string &targetId, const NodePtr &incoming, Edge edge,
                     const std::string &splitId, double share)
{
    if (!root)
        return incoming;
    share = std::min(0.9, std::max(0.1, share));
    if (root->id == targetId) {
        const bool before = edgeBefore(edge);
        return normalize(makeSplit(splitId, edgeAxis(edge),
                                   before ? std::vector<NodePtr>{incoming, root} : std::vector<NodePtr>{root, incoming},
                                   before ? std::vector<double>{share, 1 - share}
                                          : std::vector<double>{1 - share, share}));
    }
    if (root->kind == Node::Kind::Panel)
        return root;
    if (root->kind == Node::Kind::Stage) {
        if (!root->child)
            return root;
        NodePtr child = insertBeside(root->child, targetId, incoming, edge, splitId, share);
        return child == root->child ? root : withChild(*root, child);
    }
    bool changed = false;
    std::vector<NodePtr> children;
    for (const auto &c : root->children) {
        NodePtr next = insertBeside(c, targetId, incoming, edge, splitId, share);
        if (next != c)
            changed = true;
        children.push_back(next);
    }
    return changed ? normalize(withSplit(*root, children, root->weights)) : root;
}

NodePtr replaceNode(const NodePtr &root, const std::string &id, const NodePtr &replacement)
{
    if (!root)
        return root;
    if (root->id == id)
        return replacement;
    if (root->kind == Node::Kind::Panel)
        return root;
    if (root->kind == Node::Kind::Stage) {
        if (!root->child)
            return root;
        NodePtr child = replaceNode(root->child, id, replacement);
        return child == root->child ? root : withChild(*root, child);
    }
    bool changed = false;
    std::vector<NodePtr> children;
    for (const auto &c : root->children) {
        NodePtr next = replaceNode(c, id, replacement);
        if (next != c)
            changed = true;
        children.push_back(next);
    }
    return changed ? withSplit(*root, children, root->weights) : root;
}

NodePtr flipBoundary(const NodePtr &root, const std::string &splitId, int index, const std::string &wrapperId)
{
    const NodePtr split = findNode(root, splitId);
    const int n = split ? int(split->children.size()) : 0;
    if (!split || split->kind != Node::Kind::Split || index < 0 || index + 1 >= n)
        return root;
    const Axis other = split->axis == Axis::X ? Axis::Y : Axis::X;
    if (n == 2)
        return normalize(replaceNode(root, splitId, makeSplit(split->id, other, split->children, split->weights)));
    const double a = split->weights[index], b = split->weights[index + 1];
    std::vector<NodePtr> children;
    std::vector<double> weights;
    for (int i = 0; i < n; ++i) {
        if (i == index) {
            children.push_back(makeSplit(wrapperId, other, {split->children[i], split->children[i + 1]}, {a, b}));
            weights.push_back(a + b);
        } else if (i != index + 1) {
            children.push_back(split->children[i]);
            weights.push_back(split->weights[i]);
        }
    }
    return normalize(replaceNode(root, splitId, makeSplit(split->id, split->axis, children, weights)));
}

NodePtr resizeBoundary(const NodePtr &split, int index, double position, const std::function<double(int)> &min)
{
    auto minOf = [&](int i) { return min ? min(i) : 0.04; };
    const double total = sum(split->weights);
    std::vector<double> normalized;
    for (double w : split->weights)
        normalized.push_back(w / total);
    double start = 0;
    for (int i = 0; i < index; ++i)
        start += normalized[i];
    const double pair = normalized[index] + normalized[index + 1];
    const double lo = std::min(minOf(index), pair / 2);
    const double hi = pair - std::min(minOf(index + 1), pair / 2);
    const double left = std::max(lo, std::min(hi, position - start));
    normalized[index] = left;
    normalized[index + 1] = pair - left;
    return withSplit(*split, split->children, normalized);
}

} // namespace krellis
