// Document operations (port of Trellis model/document).
#pragma once

#include "tree.h"

#include <functional>
#include <optional>

namespace trellis {

std::string uid(const std::string &prefix);
LayoutDocument emptyDocument();

struct PanelLocation {
    enum class Where { None, Docked, Floating, Hidden };
    Where where = Where::None;
    NodePtr panel;
    FloatingPanel floatState; // Floating
    RestoreTarget restore;    // Hidden
    explicit operator bool() const { return where != Where::None; }
};

std::vector<NodePtr> allPanels(const LayoutDocument &doc);
PanelLocation locatePanel(const LayoutDocument &doc, const std::string &panelId);
NodePtr panelOfView(const LayoutDocument &doc, const std::string &viewId);

NodePtr withViews(const NodePtr &panel, std::vector<std::string> views, std::string selected);

LayoutDocument updatePanel(LayoutDocument doc, NodePtr panel);
LayoutDocument removePanel(LayoutDocument doc, const std::string &panelId, bool keepProportions = false);
/// A vacancy is a docked panel with no views: the slot a window left when it was pulled out into float. It keeps
/// its size so the neighbours stay put, and takes the next window dropped or opened there.
bool isVacant(const NodePtr &node);
/// Docked vacancies, in layout order.
std::vector<std::string> vacancies(const NodePtr &root);
/// Replace a docked panel with a vacancy; nullopt when it has no split parent to keep a slot in.
std::optional<LayoutDocument> vacatePanel(const LayoutDocument &doc, const std::string &panelId);
/// Put `panel` in the vacancy's slot.
LayoutDocument fillVacancy(LayoutDocument doc, const std::string &vacancyId, NodePtr panel);
LayoutDocument detachView(LayoutDocument doc, const std::string &viewId);
LayoutDocument closeView(LayoutDocument doc, const std::string &viewId);
LayoutDocument selectView(LayoutDocument doc, const std::string &viewId);
LayoutDocument addTab(LayoutDocument doc, const std::string &panelId, const std::string &viewId,
                      std::optional<int> index = std::nullopt, bool select = true);
LayoutDocument reorderTabs(LayoutDocument doc, const std::string &panelId, const std::vector<std::string> &order);

struct DockTarget {
    bool into = false;
    std::string id; // `beside` or `into`
    Edge edge = Edge::Left;
    std::optional<double> share;
    std::optional<int> index;
    static DockTarget beside(std::string id, Edge e, std::optional<double> share = std::nullopt)
    {
        DockTarget t;
        t.id = std::move(id);
        t.edge = e;
        t.share = share;
        return t;
    }
    static DockTarget intoPanel(std::string id, std::optional<int> index = std::nullopt)
    {
        DockTarget t;
        t.into = true;
        t.id = std::move(id);
        t.index = index;
        return t;
    }
};

LayoutDocument insertPanel(LayoutDocument doc, NodePtr panel, const DockTarget &target);
LayoutDocument floatPanel(LayoutDocument doc, NodePtr panel, Rect rect, FloatingLayer layer);
Rect clampFloat(Rect rect);
/// Overlay floats are confined to the usable area (never over a panel or off screen); stage floats keep the loose rule.
Rect clampFloat(Rect rect, FloatingLayer layer, const FloatMargin &margin = {});
LayoutDocument raiseFloat(LayoutDocument doc, const std::string &panelId);

std::optional<RestoreTarget> restoreTargetFor(const LayoutDocument &doc, const std::string &panelId);
NodePtr parentSplit(const NodePtr &root, const std::string &id);
/// Flips a docked panel and its next sibling (the previous one for the last) between side-by-side and stacked.
/// Null when the panel is not docked in a split.
std::optional<LayoutDocument> flipPanel(const LayoutDocument &doc, const std::string &panelId);

LayoutDocument hidePanel(LayoutDocument doc, const std::string &panelId);
LayoutDocument restorePanel(LayoutDocument doc, const std::string &panelId, FloatingLayer fallbackLayer);

/// Repair anything inconsistent in a document from storage or a caller.
LayoutDocument sanitize(LayoutDocument input, const std::function<bool(const std::string &)> &isKnownType = nullptr);
std::vector<std::string> viewIds(const LayoutDocument &doc);

} // namespace trellis
