// Layout tree operations (port of Trellis model/tree).
#pragma once

#include "types.h"

#include <functional>

namespace krellis {

inline const Rect UNIT{0, 0, 1, 1};

struct Entry {
    NodePtr node;
    Rect rect;
    std::string parent; // empty = none
    double scale = 1;
    bool isVirtual = false; // camera-only sibling range (spatial.h)
};
using Entries = OrderedMap<Entry>;

/// Pixel minimums for laying out. `min` is the smallest a node may be along an axis, in pixels
/// of its parent group's own layout space.
struct LayoutMetrics {
    double width = 0, height = 0;
    std::function<double(const Node &, Axis)> min;
};

Entries layoutRects(const NodePtr &root, const LayoutMetrics *metrics = nullptr);
std::vector<double> distribute(double total, const std::vector<double> &weights, const std::vector<double> &mins);
double sum(const std::vector<double> &values);

NodePtr findNode(const NodePtr &node, const std::string &id);
std::vector<NodePtr> panelsOf(const NodePtr &node);
NodePtr findStage(const NodePtr &node);
bool contains(const NodePtr &root, const std::string &ancestor, const std::string &id);

NodePtr normalize(const NodePtr &node);
/// Removes a node. The freed share goes to the following sibling, or with keepProportions to all siblings
/// in proportion, so nothing else changes size relative to its neighbours.
NodePtr removeNode(const NodePtr &root, const std::string &id, bool keepProportions = false);
/// `nested` marks the new split as a dwindle group (Node::nested): it is not flattened into a same-axis parent.
NodePtr insertBeside(const NodePtr &root, const std::string &targetId, const NodePtr &incoming, Edge edge,
                     const std::string &splitId, double share = 0.5, bool nested = false);
NodePtr replaceNode(const NodePtr &root, const std::string &id, const NodePtr &replacement);
/// Turns the pair of siblings either side of a boundary from side-by-side to stacked or back. A split of exactly
/// two is toggled in place; in a longer one the pair is wrapped in a new split of the other axis (`wrapperId`),
/// which a second flip dissolves again. Returns the root unchanged when there is no such boundary.
NodePtr flipBoundary(const NodePtr &root, const std::string &splitId, int index, const std::string &wrapperId);
NodePtr resizeBoundary(const NodePtr &split, int index, double position,
                       const std::function<double(int)> &min = nullptr);

} // namespace krellis
