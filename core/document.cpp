#include "document.h"
#include "spatial.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <set>

namespace trellis {

std::string uid(const std::string &prefix)
{
    static std::atomic<unsigned> counter{0};
    static std::mt19937 rng{std::random_device{}()};
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string random;
    for (int i = 0; i < 5; ++i)
        random += alphabet[rng() % 36];
    return prefix + "-" + random + std::to_string(++counter % 1000000);
}

LayoutDocument emptyDocument()
{
    return {};
}

std::vector<NodePtr> allPanels(const LayoutDocument &doc)
{
    std::vector<NodePtr> out = panelsOf(doc.root);
    for (const auto &f : doc.floating)
        out.push_back(f.panel);
    for (const auto &h : doc.hidden)
        out.push_back(h.panel);
    return out;
}

PanelLocation locatePanel(const LayoutDocument &doc, const std::string &panelId)
{
    PanelLocation loc;
    if (auto docked = findNode(doc.root, panelId); docked && docked->kind == Node::Kind::Panel) {
        loc.where = PanelLocation::Where::Docked;
        loc.panel = docked;
        return loc;
    }
    for (const auto &f : doc.floating)
        if (f.panel->id == panelId) {
            loc.where = PanelLocation::Where::Floating;
            loc.panel = f.panel;
            loc.floatState = f;
            return loc;
        }
    for (const auto &h : doc.hidden)
        if (h.panel->id == panelId) {
            loc.where = PanelLocation::Where::Hidden;
            loc.panel = h.panel;
            loc.restore = h.restore;
            return loc;
        }
    return loc;
}

NodePtr panelOfView(const LayoutDocument &doc, const std::string &viewId)
{
    for (const auto &p : allPanels(doc))
        if (std::find(p->views.begin(), p->views.end(), viewId) != p->views.end())
            return p;
    return nullptr;
}

NodePtr withViews(const NodePtr &panel, std::vector<std::string> views, std::string selected)
{
    auto n = std::make_shared<Node>(*panel);
    n->views = std::move(views);
    n->selected = std::move(selected);
    return n;
}

static bool has(const std::vector<std::string> &v, const std::string &s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

LayoutDocument updatePanel(LayoutDocument doc, NodePtr panel)
{
    if (panel->views.empty())
        return removePanel(std::move(doc), panel->id);
    if (!has(panel->views, panel->selected))
        panel = withViews(panel, panel->views, panel->views.front());
    const auto loc = locatePanel(doc, panel->id);
    if (!loc)
        return doc;
    if (loc.where == PanelLocation::Where::Docked) {
        doc.root = replaceNode(doc.root, panel->id, panel);
    } else if (loc.where == PanelLocation::Where::Floating) {
        for (auto &f : doc.floating)
            if (f.panel->id == panel->id)
                f.panel = panel;
    } else {
        for (auto &h : doc.hidden)
            if (h.panel->id == panel->id)
                h.panel = panel;
    }
    return doc;
}

LayoutDocument removePanel(LayoutDocument doc, const std::string &panelId, bool keepProportions)
{
    const auto loc = locatePanel(doc, panelId);
    if (!loc)
        return doc;
    if (loc.where == PanelLocation::Where::Docked) {
        doc.root = removeNode(doc.root, panelId, keepProportions);
    } else if (loc.where == PanelLocation::Where::Floating) {
        doc.floating.erase(std::remove_if(doc.floating.begin(), doc.floating.end(),
                                          [&](const FloatingPanel &f) { return f.panel->id == panelId; }),
                           doc.floating.end());
    } else {
        doc.hidden.erase(std::remove_if(doc.hidden.begin(), doc.hidden.end(),
                                        [&](const HiddenPanel &h) { return h.panel->id == panelId; }),
                         doc.hidden.end());
    }
    return doc;
}

bool isVacant(const NodePtr &node)
{
    return node && node->kind == Node::Kind::Panel && node->vacant;
}

std::vector<std::string> vacancies(const NodePtr &root)
{
    std::vector<std::string> out;
    for (const auto &id : leafIds(root))
        if (isVacant(findNode(root, id)))
            out.push_back(id);
    return out;
}

std::optional<LayoutDocument> vacatePanel(const LayoutDocument &doc, const std::string &panelId)
{
    const NodePtr parent = parentSplit(doc.root, panelId);
    const NodePtr panel = findNode(doc.root, panelId);
    if (!parent || !panel || panel->kind != Node::Kind::Panel)
        return std::nullopt;
    LayoutDocument out = doc;
    out.root = replaceNode(doc.root, panelId, makeVacancy(uid("vacancy")));
    return out;
}

LayoutDocument fillVacancy(LayoutDocument doc, const std::string &vacancyId, NodePtr panel)
{
    doc.root = replaceNode(doc.root, vacancyId, panel);
    return doc;
}

LayoutDocument detachView(LayoutDocument doc, const std::string &viewId)
{
    NodePtr panel = panelOfView(doc, viewId);
    if (!panel)
        return doc;
    std::vector<std::string> views;
    for (const auto &v : panel->views)
        if (v != viewId)
            views.push_back(v);
    std::string selected = panel->selected;
    if (selected == viewId) {
        const auto index = std::find(panel->views.begin(), panel->views.end(), viewId) - panel->views.begin();
        selected = views.empty() ? std::string() : views[std::min<size_t>(index, views.size() - 1)];
    }
    return updatePanel(std::move(doc), withViews(panel, views, selected));
}

LayoutDocument closeView(LayoutDocument doc, const std::string &viewId)
{
    doc = detachView(std::move(doc), viewId);
    doc.views.erase(viewId);
    return doc;
}

LayoutDocument selectView(LayoutDocument doc, const std::string &viewId)
{
    NodePtr panel = panelOfView(doc, viewId);
    if (!panel || panel->selected == viewId)
        return doc;
    return updatePanel(std::move(doc), withViews(panel, panel->views, viewId));
}

LayoutDocument addTab(LayoutDocument doc, const std::string &panelId, const std::string &viewId,
                      std::optional<int> index, bool select)
{
    const auto loc = locatePanel(doc, panelId);
    if (!loc)
        return doc;
    std::vector<std::string> views;
    for (const auto &v : loc.panel->views)
        if (v != viewId)
            views.push_back(v);
    const int at = index ? std::max(0, std::min<int>(*index, views.size())) : int(views.size());
    views.insert(views.begin() + at, viewId);
    std::string selected = select ? viewId : (loc.panel->selected.empty() ? viewId : loc.panel->selected);
    return updatePanel(std::move(doc), withViews(loc.panel, views, selected));
}

LayoutDocument reorderTabs(LayoutDocument doc, const std::string &panelId, const std::vector<std::string> &order)
{
    const auto loc = locatePanel(doc, panelId);
    if (!loc)
        return doc;
    const auto &views = loc.panel->views;
    if (order.size() != views.size())
        return doc;
    for (const auto &id : order)
        if (!has(views, id))
            return doc;
    return updatePanel(std::move(doc), withViews(loc.panel, order, loc.panel->selected));
}

static LayoutDocument mergeInto(LayoutDocument doc, const NodePtr &panel, const std::string &intoId,
                                std::optional<int> index)
{
    const auto loc = locatePanel(doc, intoId);
    if (!loc)
        return doc;
    auto views = loc.panel->views;
    const int at = index ? std::min<int>(*index, views.size()) : int(views.size());
    views.insert(views.begin() + std::max(0, at), panel->views.begin(), panel->views.end());
    return updatePanel(std::move(doc), withViews(loc.panel, views, panel->selected));
}

LayoutDocument insertPanel(LayoutDocument doc, NodePtr panel, const DockTarget &target)
{
    if (target.into) {
        NodePtr node = findNode(doc.root, target.id);
        if (node && node->kind == Node::Kind::Stage) {
            if (!node->child) {
                auto stage = std::make_shared<Node>(*node);
                stage->child = panel;
                doc.root = replaceNode(doc.root, node->id, stage);
                return doc;
            }
            const auto first = panelsOf(node->child).front();
            return mergeInto(std::move(doc), panel, first->id, target.index);
        }
        return mergeInto(std::move(doc), panel, target.id, target.index);
    }
    if (!doc.root) {
        doc.root = panel;
        return doc;
    }
    if (!findNode(doc.root, target.id))
        return insertPanel(std::move(doc), panel, DockTarget::beside(doc.root->id, target.edge));
    doc.root = insertBeside(doc.root, target.id, panel, target.edge, uid("split"), target.share.value_or(0.5));
    return doc;
}

Rect clampFloat(Rect rect)
{
    const double w = std::min(1.0, std::max(0.02, rect.w));
    const double h = std::min(1.0, std::max(0.02, rect.h));
    return {std::min(1 - std::min(w, 0.05), std::max(-w + 0.05, rect.x)),
            std::min(1 - std::min(h, 0.05), std::max(0.0, rect.y)), w, h};
}

Rect clampFloat(Rect rect, FloatingLayer layer, const FloatMargin &margin)
{
    if (layer != FloatingLayer::Overlay)
        return clampFloat(rect);
    const double minX = -margin.left, maxX = 1 + margin.right, minY = -margin.top, maxY = 1 + margin.bottom;
    const double w = std::min(maxX - minX, std::max(0.02, rect.x < minX ? rect.w + (rect.x - minX) : rect.w));
    const double h = std::min(maxY - minY, std::max(0.02, rect.y < minY ? rect.h + (rect.y - minY) : rect.h));
    const double x = std::min(maxX - w, std::max(minX, rect.x));
    const double y = std::min(maxY - h, std::max(minY, rect.y));
    return {x, y, std::min(w, maxX - x), std::min(h, maxY - y)};
}

static int topZ(const LayoutDocument &doc)
{
    int top = 0;
    for (const auto &f : doc.floating)
        top = std::max(top, f.z);
    return top;
}

LayoutDocument floatPanel(LayoutDocument doc, NodePtr panel, Rect rect, FloatingLayer layer)
{
    doc = removePanel(std::move(doc), panel->id);
    const int z = topZ(doc) + 1;
    doc.floating.push_back({panel, clampFloat(rect, layer, doc.floatMargin), z, layer});
    return doc;
}

LayoutDocument raiseFloat(LayoutDocument doc, const std::string &panelId)
{
    const int top = topZ(doc);
    for (auto &f : doc.floating)
        if (f.panel->id == panelId) {
            if (f.z == top)
                return doc;
            f.z = top + 1;
            return doc;
        }
    return doc;
}

std::optional<LayoutDocument> flipPanel(const LayoutDocument &doc, const std::string &panelId)
{
    const NodePtr parent = parentSplit(doc.root, panelId);
    if (!parent)
        return std::nullopt;
    int at = 0;
    while (parent->children[at]->id != panelId)
        ++at;
    LayoutDocument out = doc;
    out.root = flipBoundary(doc.root, parent->id, at + 1 < int(parent->children.size()) ? at : at - 1, uid("split"));
    if (out.root == doc.root)
        return std::nullopt;
    return out;
}

NodePtr parentSplit(const NodePtr &root, const std::string &id)
{
    if (!root || root->kind == Node::Kind::Panel)
        return nullptr;
    if (root->kind == Node::Kind::Stage)
        return parentSplit(root->child, id);
    for (const auto &c : root->children)
        if (c->id == id)
            return root;
    for (const auto &c : root->children)
        if (auto f = parentSplit(c, id))
            return f;
    return nullptr;
}

std::optional<RestoreTarget> restoreTargetFor(const LayoutDocument &doc, const std::string &panelId)
{
    const auto loc = locatePanel(doc, panelId);
    if (!loc)
        return std::nullopt;
    RestoreTarget r;
    if (loc.where == PanelLocation::Where::Floating) {
        r.kind = RestoreTarget::Kind::Floating;
        r.rect = loc.floatState.rect;
        r.layer = loc.floatState.layer;
        return r;
    }
    if (loc.where == PanelLocation::Where::Hidden)
        return loc.restore;
    NodePtr parent = parentSplit(doc.root, panelId);
    if (!parent) {
        NodePtr stage = findStage(doc.root);
        if (stage && stage->child && stage->child->id == panelId) {
            r.kind = RestoreTarget::Kind::Tab;
            r.panel = stage->id;
            return r;
        }
        r.kind = RestoreTarget::Kind::Docked;
        r.beside = doc.root ? doc.root->id : std::string();
        r.edge = Edge::Left;
        r.share = 0.5;
        return r;
    }
    size_t index = 0;
    for (size_t i = 0; i < parent->children.size(); ++i)
        if (parent->children[i]->id == panelId)
            index = i;
    const bool after = index + 1 < parent->children.size();
    const size_t neighbourIndex = after ? index + 1 : index - 1;
    const double own = parent->weights[index];
    r.kind = RestoreTarget::Kind::Docked;
    r.beside = parent->children[neighbourIndex]->id;
    r.share = own / (own + parent->weights[neighbourIndex]);
    if (parent->axis == Axis::X)
        r.edge = after ? Edge::Left : Edge::Right;
    else
        r.edge = after ? Edge::Top : Edge::Bottom;
    return r;
}

LayoutDocument hidePanel(LayoutDocument doc, const std::string &panelId)
{
    const auto loc = locatePanel(doc, panelId);
    if (!loc || loc.where == PanelLocation::Where::Hidden)
        return doc;
    const auto restore = *restoreTargetFor(doc, panelId);
    doc = removePanel(std::move(doc), panelId);
    doc.hidden.push_back({loc.panel, restore});
    return doc;
}

LayoutDocument restorePanel(LayoutDocument doc, const std::string &panelId, FloatingLayer fallbackLayer)
{
    auto it = std::find_if(doc.hidden.begin(), doc.hidden.end(),
                           [&](const HiddenPanel &h) { return h.panel->id == panelId; });
    if (it == doc.hidden.end())
        return doc;
    const HiddenPanel hidden = *it;
    doc.hidden.erase(it);
    const auto &restore = hidden.restore;
    if (restore.kind == RestoreTarget::Kind::Floating)
        return floatPanel(std::move(doc), hidden.panel, restore.rect, restore.layer);
    if (restore.kind == RestoreTarget::Kind::Tab) {
        const auto target = locatePanel(doc, restore.panel);
        const NodePtr node = findNode(doc.root, restore.panel);
        if ((target && target.where != PanelLocation::Where::Hidden) || (node && node->kind == Node::Kind::Stage))
            return insertPanel(std::move(doc), hidden.panel, DockTarget::intoPanel(restore.panel));
    }
    if (restore.kind == RestoreTarget::Kind::Docked && findNode(doc.root, restore.beside))
        return insertPanel(std::move(doc), hidden.panel, DockTarget::beside(restore.beside, restore.edge, restore.share));
    if (doc.root)
        return floatPanel(std::move(doc), hidden.panel, {0.25, 0.2, 0.5, 0.6}, fallbackLayer);
    doc.root = hidden.panel;
    return doc;
}

LayoutDocument sanitize(LayoutDocument input, const std::function<bool(const std::string &)> &isKnownType)
{
    LayoutDocument doc = std::move(input);
    std::set<std::string> seen, seenPanels;
    auto keep = [&](const std::string &id) {
        auto it = doc.views.find(id);
        if (it == doc.views.end() || it->second.type.empty() || seen.count(id))
            return false;
        if (isKnownType && !isKnownType(it->second.type))
            return false;
        seen.insert(id);
        return true;
    };
    auto fixPanel = [&](const NodePtr &panel) -> NodePtr {
        if (!panel || panel->kind != Node::Kind::Panel || seenPanels.count(panel->id))
            return nullptr;
        seenPanels.insert(panel->id);
        std::vector<std::string> views;
        for (const auto &v : panel->views)
            if (keep(v))
                views.push_back(v);
        if (views.empty())
            return nullptr;
        return withViews(panel, views, has(views, panel->selected) ? panel->selected : views.front());
    };
    int stages = 0;
    std::function<NodePtr(const NodePtr &)> fixNode = [&](const NodePtr &node) -> NodePtr {
        if (!node)
            return nullptr;
        if (node->kind == Node::Kind::Panel)
            return fixPanel(node);
        if (node->kind == Node::Kind::Stage) {
            if (stages++)
                return node->child ? fixNode(node->child) : nullptr;
            NodePtr child = node->child ? fixNode(node->child) : nullptr;
            if (child && child->kind == Node::Kind::Stage)
                child = nullptr;
            return makeStage(node->id, child);
        }
        std::vector<NodePtr> children;
        std::vector<double> weights;
        for (size_t i = 0; i < node->children.size(); ++i) {
            if (NodePtr fixed = fixNode(node->children[i])) {
                children.push_back(fixed);
                const double w = i < node->weights.size() ? node->weights[i] : 1.0;
                weights.push_back(std::isfinite(w) && w > 0 ? w : 1.0);
            }
        }
        if (children.empty())
            return nullptr;
        return normalize(makeSplit(node->id, node->axis, children, weights));
    };
    doc.root = doc.root ? fixNode(doc.root) : nullptr;
    doc.root = doc.root ? normalize(doc.root) : nullptr;

    std::vector<FloatingPanel> floating;
    for (auto &f : doc.floating)
        if (NodePtr panel = fixPanel(f.panel)) {
            f.panel = panel;
            f.rect = clampFloat(f.rect, f.layer, doc.floatMargin);
            floating.push_back(f);
        }
    doc.floating = std::move(floating);
    std::vector<HiddenPanel> hidden;
    for (auto &h : doc.hidden)
        if (NodePtr panel = fixPanel(h.panel)) {
            h.panel = panel;
            hidden.push_back(h);
        }
    doc.hidden = std::move(hidden);
    for (auto it = doc.views.begin(); it != doc.views.end();)
        it = seen.count(it->first) ? std::next(it) : doc.views.erase(it);
    return doc;
}

std::vector<std::string> viewIds(const LayoutDocument &doc)
{
    std::vector<std::string> out;
    for (const auto &p : allPanels(doc))
        out.insert(out.end(), p->views.begin(), p->views.end());
    return out;
}

} // namespace trellis
