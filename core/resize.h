// Divider drags that push (port of Trellis model/resize).
#pragma once

#include "tree.h"

#include <functional>
#include <memory>

namespace trellis {

struct BoundaryDrag {
    Axis axis = Axis::X;
    double start = 0, min = 0, max = 0;
    /// The layout with the divider at `position` (clamped), pushing what it has to.
    std::function<NodePtr(double)> to;
};

/// nullptr-like result is `std::nullopt`-style: check `valid`.
struct BoundaryDragResult {
    bool valid = false;
    BoundaryDrag drag;
};

BoundaryDragResult dragBoundary(const NodePtr &root, const LayoutMetrics &metrics, const std::string &splitId,
                                int index);

} // namespace trellis
