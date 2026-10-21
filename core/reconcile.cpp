#include "reconcile.h"

#include "spatial.h"

#include <algorithm>

namespace trellis {

namespace {
bool contains(const std::set<std::string> &set, const std::string &v) { return set.count(v) != 0; }

LayoutDocument dockAtEnd(LayoutDocument doc, const NodePtr &panel)
{
    if (doc.root && doc.root->kind == Node::Kind::Split && doc.root->axis == Axis::X) {
        const auto spec = seamTarget(doc.root, int(doc.root->children.size()));
        doc.root = applyDockTarget(doc.root, spec, panel, uid("split"));
    } else if (doc.root) {
        doc = insertPanel(std::move(doc), panel, DockTarget::beside(leafIds(doc.root).back(), Edge::Right));
    } else {
        doc.root = panel;
    }
    return doc;
}
}

LayoutDocument reconcile(const LayoutDocument &snapshot, const LayoutDocument &now, const std::set<std::string> &fromNow)
{
    LayoutDocument out = snapshot;

    const auto liveList = viewIds(now);
    const std::set<std::string> live(liveList.begin(), liveList.end());
    for (const auto &v : viewIds(snapshot))
        if (!contains(live, v))
            out = closeView(std::move(out), v);

    std::set<std::string> placed;
    for (const auto &panel : allPanels(now)) {
        if (panel->vacant || !placed.insert(panel->id).second)
            continue;
        const auto nowLoc = locatePanel(now, panel->id);
        const auto oldLoc = locatePanel(out, panel->id);
        const bool nowHidden = nowLoc.where == PanelLocation::Where::Hidden;
        const bool oldHidden = oldLoc.where == PanelLocation::Where::Hidden;
        const bool keep = oldLoc && nowHidden == oldHidden && !contains(fromNow, panel->id);
        if (keep)
            continue;
        if (oldLoc)
            out = removePanel(std::move(out), panel->id);
        switch (nowLoc.where) {
        case PanelLocation::Where::Docked:
            out = dockAtEnd(std::move(out), nowLoc.panel);
            break;
        case PanelLocation::Where::Floating:
            out = floatPanel(std::move(out), nowLoc.panel, nowLoc.floatState.rect, nowLoc.floatState.layer);
            break;
        case PanelLocation::Where::Hidden:
            out.hidden.push_back({nowLoc.panel, nowLoc.restore});
            break;
        case PanelLocation::Where::None:
            break;
        }
    }

    out.floatMargin = now.floatMargin;
    out.navigation = now.navigation;
    out.views = now.views;
    for (auto &f : out.floating)
        for (const auto &g : now.floating)
            if (g.panel->id == f.panel->id)
                f.z = g.z;
    return out;
}

} // namespace trellis
