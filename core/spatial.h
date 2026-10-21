// Docking and navigation geometry (port of Trellis model/spatial).
#pragma once

#include "tree.h"

#include <optional>

namespace trellis {

/// Panels and empty stages: the leaves a view can sit beside.
std::vector<std::string> leafIds(const NodePtr &node);

/// Camera targets: every node plus contiguous sibling ranges of splits with 3+ children.
Entries focusLayout(const NodePtr &root, const LayoutMetrics *metrics = nullptr);
std::string rangeId(const std::string &splitId, const std::vector<std::string> &children);

std::string navParent(const Entries &entries, const std::string &id); // empty = none
std::string navNode(const Entries &entries, const std::string &id);
bool containsNode(const NodePtr &node, const std::string &id);
bool sameRect(const Rect &a, const Rect &b, double eps = 1e-9);

std::string bestFit(const Rect &camera, const Entries &entries);

struct Point {
    double x = 0, y = 0;
};
enum class Direction { In, Out };
std::string hierarchyStep(const Entries &entries, const std::string &focused, Direction dir, Point point);

std::string frameLeaves(const NodePtr &root, const std::vector<std::string> &ids); // empty = none
Rect rectangleCamera(Point a, Point b, const Rect &viewport, const Rect &camera);

struct Visit {
    std::string id;
    std::vector<std::string> leaves;
};
struct MaximizeSession {
    std::string id;
    std::vector<std::string> returnPath;
};
struct VisitHistory {
    std::vector<Visit> visits;
    int index = 0;
};
VisitHistory recordVisit(const std::vector<Visit> &visits, int index, const Visit &visit);
std::string savedFrameDestination(const NodePtr &root, const std::vector<std::string> &leaves);

struct MaximizeResult {
    std::string destination;
    std::optional<MaximizeSession> session;
};
MaximizeResult maximizeTransition(const Entries &entries, const std::string &rootId, const std::string &focused,
                                  const std::string &id, const std::optional<MaximizeSession> &session);

// ---------------------------------------------------------------- docking
struct SplitSeam {
    Axis axis = Axis::X;
    std::vector<std::string> leaves;
    std::vector<std::string> before;
};
struct DockTargetSpec {
    std::string id;
    Edge edge = Edge::Left;
    std::optional<SplitSeam> seam;
    int boundary = 0;
};

std::optional<Edge> dropEdge(Point point, const Rect &rect);
struct DropRects {
    Rect slot, remaining, collapsed;
};
DropRects dropRects(const Rect &rect, Edge edge);
DockTargetSpec seamTarget(const NodePtr &group, int boundary);
DockTargetSpec tileDropTarget(const Entries &entries, const std::string &id, Edge edge);
std::optional<DockTargetSpec> frameDropTarget(const NodePtr &group, Point point, double width, double height,
                                              double radius);
NodePtr insertAtSeam(const NodePtr &root, const SplitSeam &seam, const NodePtr &incoming, const std::string &splitId);
NodePtr applyDockTarget(const NodePtr &root, const DockTargetSpec &target, const NodePtr &incoming,
                        const std::string &splitId);

} // namespace trellis
