#include "drag.h"

#include "document.h"

#include <algorithm>
#include <cmath>

namespace trellis {

namespace {

bool inside(Point p, const Rect &r)
{
    return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
}

/// Same split structure and window order, ignoring weights and split ids. The incoming id stands for the lifted panel.
bool sameShape(const NodePtr &a, const NodePtr &b, const std::string &incoming, const std::string &lifted)
{
    if (!a || !b)
        return !a && !b;
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
    case Node::Kind::Panel:
        return a->id == (b->id == incoming ? lifted : b->id);
    case Node::Kind::Stage:
        return a->id == b->id && sameShape(a->child, b->child, incoming, lifted);
    case Node::Kind::Split:
        if (a->axis != b->axis || a->children.size() != b->children.size())
            return false;
        for (size_t i = 0; i < a->children.size(); ++i)
            if (!sameShape(a->children[i], b->children[i], incoming, lifted))
                return false;
        return true;
    }
    return false;
}

NodePtr renameNode(const NodePtr &node, const std::string &from, const std::string &to)
{
    if (!node)
        return node;
    auto copy = std::make_shared<Node>(*node);
    if (node->id == from)
        copy->id = to;
    if (node->kind == Node::Kind::Stage)
        copy->child = renameNode(node->child, from, to);
    for (auto &c : copy->children)
        c = renameNode(c, from, to);
    return copy;
}

std::string join(const std::vector<std::string> &v)
{
    std::string s;
    for (const auto &x : v)
        s += x + ",";
    return s;
}

std::string seamKey(const DockTargetSpec &spec)
{
    if (!spec.seam)
        return {};
    return std::to_string(int(spec.seam->axis)) + "|" + join(spec.seam->before) + "|" + join(spec.seam->leaves);
}

Rect unionOf(const std::vector<Rect> &rects)
{
    double x = 1e18, y = 1e18, r = -1e18, b = -1e18;
    for (const auto &q : rects) {
        x = std::min(x, q.x);
        y = std::min(y, q.y);
        r = std::max(r, q.x + q.w);
        b = std::max(b, q.y + q.h);
    }
    return {x, y, r - x, b - y};
}

} // namespace

bool DropTarget::operator==(const DropTarget &o) const
{
    if (kind != o.kind)
        return false;
    switch (kind) {
    case Kind::Tab:
        return panel == o.panel && index == o.index;
    case Kind::Stage:
        return stage == o.stage;
    case Kind::Dock:
        return spec.id == o.spec.id && spec.edge == o.spec.edge && seamKey(spec) == seamKey(o.spec);
    case Kind::Float:
        return true;
    }
    return true;
}

static bool sameTarget(const std::optional<DropTarget> &a, const std::optional<DropTarget> &b)
{
    if (!a || !b)
        return a.has_value() == b.has_value();
    return *a == *b;
}

DragSession::DragSession(DragHost &host, const std::string &panelId, const std::string &viewId, Point pointer)
    : m_host(host)
    , m_start(pointer)
    , m_pointer(pointer)
    , m_origin(host.doc())
    , m_pdoc(host.doc())
{
    auto loc = locatePanel(m_origin, panelId);
    m_lifted = loc.panel;
    if (loc.where == PanelLocation::Where::Floating)
        m_fromFloat = loc.floatState.layer;
    if (!viewId.empty() && m_lifted && m_lifted->views.size() > 1)
        m_pendingTab = viewId;
    m_fromFrame = host.frameOnly(panelId);
    auto it = host.lastRects().find(panelId);
    m_sourceScreen = it != host.lastRects().end() ? it->second : Rect{};
    m_floating = m_sourceScreen;
    m_grabX = m_sourceScreen.w ? (pointer.x - m_sourceScreen.x) / m_sourceScreen.w : 0.5;
    m_grabY = pointer.y - m_sourceScreen.y;
}

bool DragSession::move(Point pointer, double nowMs)
{
    m_now = nowMs;
    if (!m_active) {
        if (std::hypot(pointer.x - m_start.x, pointer.y - m_start.y) < Threshold)
            return false;
        activate(nowMs);
    }
    m_pointer = pointer;
    retarget();
    return true;
}

void DragSession::activate(double nowMs)
{
    m_active = true;
    LayoutDocument doc = m_host.doc();
    if (!m_pendingTab.empty()) {
        // Tearing a tab out: it becomes its own window at the host's position; cancelling restores.
        auto it = m_host.lastRects().find(m_lifted->id);
        Rect origin = it != m_host.lastRects().end() ? it->second : m_sourceScreen;
        const std::string hostPanel = m_lifted->id;
        doc = detachView(doc, m_pendingTab);
        m_lifted = makePanel(uid("panel"), {m_pendingTab}, m_pendingTab);
        m_inDoc = false;
        m_excludedHost = hostPanel;
        m_tabOrigin = origin;
        m_tabRollback = true;
        if (!m_fromFloat)
            m_fromFloat = FloatingLayer::Stage;
        m_fromFrame = false;
        m_sourceScreen = origin;
        m_pendingTab.clear();
        m_host.setDoc(doc);
    }
    m_pdoc = doc;
    const auto metrics = m_host.layoutMetrics();
    m_dropBase = layoutRects(doc.root, &metrics);
    m_base.clear();
    for (const auto &kv : m_dropBase)
        m_base[kv.first] = kv.second.rect;
    m_layoutTargets = restingLayout();
    m_pickupAt = nowMs;
    m_sizeTick = nowMs;
    m_floating = m_sourceScreen;
}

std::optional<DropTarget> DragSession::tabTarget(const NodePtr &panel, Point p, const Rect &screen) const
{
    const double bar = m_host.tabbarHeight(*panel);
    if (!bar || !(p.x >= screen.x && p.x <= screen.x + screen.w && p.y >= screen.y && p.y <= screen.y + bar))
        return std::nullopt;
    DropTarget t;
    t.kind = DropTarget::Kind::Tab;
    t.panel = panel->id;
    t.index = m_host.tabIndexAt(*panel, p.x);
    return t;
}

std::optional<Edge> DragSession::overlayEdge(Point p, const Rect &r) const
{
    const double x = (p.x - r.x) / r.w, y = (p.y - r.y) / r.h;
    std::pair<Edge, double> edges[] = {{Edge::Left, x}, {Edge::Right, 1 - x}, {Edge::Top, y}, {Edge::Bottom, 1 - y}};
    auto best = *std::min_element(std::begin(edges), std::end(edges),
                                  [](const auto &a, const auto &b) { return a.second < b.second; });
    return best.second <= OverlayEdgeBand ? std::optional<Edge>(best.first) : std::nullopt;
}

std::optional<DropTarget> DragSession::edgeDock() const
{
    const Point vp = m_host.viewport();
    const Point p = m_pointer;
    const double right = vp.x - p.x, bottom = vp.y - p.y;
    if (p.x > DockEdgePx && right > DockEdgePx && p.y > DockEdgePx && bottom > DockEdgePx)
        return std::nullopt;
    DropTarget t;
    t.kind = DropTarget::Kind::Dock;
    const LayoutDocument &doc = m_pdoc;
    NodePtr group = m_host.framedNode() ? m_host.framedNode() : doc.root;
    auto leaves = group ? leafIds(group) : std::vector<std::string>();
    if (std::none_of(leaves.begin(), leaves.end(), [&](const auto &id) { return id != m_lifted->id; }))
        return std::nullopt; // nothing tiled to dock into: the window floats and snaps to a half or quarter
    // A corner is an edge plus a wider stretch along the perpendicular one.
    const bool cl = p.x <= DockCornerPx, cr = right <= DockCornerPx, ct = p.y <= DockCornerPx, cb = bottom <= DockCornerPx;
    if ((cl != cr) && (ct != cb)) {
        const Point corner = m_host.fromScreen({cl ? 1.0 : vp.x - 1, ct ? 1.0 : vp.y - 1});
        for (const auto &id : leaves) {
            const Entry *e = id == m_lifted->id ? nullptr : m_dropBase.find(id);
            if (e && inside(corner, e->rect)) {
                t.spec = tileDropTarget(m_dropBase, id, ct ? Edge::Top : Edge::Bottom);
                return t;
            }
        }
    }
    auto frame = frameDropTarget(group, p, vp.x, vp.y, DockEdgePx);
    if (!frame)
        return std::nullopt;
    t.spec = *frame;
    return t;
}

void DragSession::retarget()
{
    const Point viewport = m_host.viewport();
    const Point p = m_pointer;
    const Point point = m_host.fromScreen(p);
    const bool within = p.x >= 0 && p.y >= 0 && p.x <= viewport.x && p.y <= viewport.y;
    const auto layer = m_host.floatingLayer();
    const LayoutDocument &doc = m_pdoc;
    NodePtr stage = findStage(doc.root);
    const bool stageIsDesktop = stage && !stage->child && layer == FloatingLayer::Stage;
    const bool overlayMove = m_fromFloat == FloatingLayer::Overlay;
    std::optional<Rect> stageWorld;
    if (stage && stage->child)
        if (const Entry *e = m_dropBase.find(stage->id))
            stageWorld = e->rect;
    const bool stageMove = m_fromFloat == FloatingLayer::Stage && !m_tabRollback && stageWorld && inside(point, *stageWorld);
    const bool floatMove = overlayMove || stageMove;
    std::optional<DropTarget> next;
    const RectMap &last = m_host.lastRects();

    // Overlay floats are on top of everything; their centres take tabs. A mode that decides the drop skips this, and so does a host without tabs.
    if (within && m_floatFirst == m_forceFloat && m_host.tabsAllowed()) {
        std::vector<FloatingPanel> overlays;
        for (const auto &f : doc.floating)
            if (f.layer == FloatingLayer::Overlay && f.panel->id != m_lifted->id)
                overlays.push_back(f);
        std::sort(overlays.begin(), overlays.end(), [](const auto &a, const auto &b) { return a.z > b.z; });
        for (const auto &f : overlays) {
            auto it = last.find(f.panel->id);
            if (it == last.end() || !inside(p, it->second))
                continue;
            next = tabTarget(f.panel, p, it->second);
            if (!next && !dropEdge(p, it->second)) {
                DropTarget t;
                t.kind = DropTarget::Kind::Tab;
                t.panel = f.panel->id;
                next = t;
            }
            return settle(filter(next), m_now);
        }
    }

    // Docked views under the pointer (stable pre-preview regions).
    std::string hovered;
    if (within)
        for (const auto &id : leafIds(doc.root)) {
            if (id == m_lifted->id)
                continue;
            const Entry *e = m_dropBase.find(id);
            if (e && inside(point, e->rect)) {
                hovered = id;
                break;
            }
        }
    NodePtr hoveredNode = hovered.empty() ? nullptr : findNode(doc.root, hovered);
    // An empty slot takes the window whole; it never splits or tabs.
    std::optional<DropTarget> vacancy;
    if (isVacant(hoveredNode)) {
        DropTarget t;
        t.kind = DropTarget::Kind::Tab;
        t.panel = hovered;
        vacancy = t;
        hoveredNode = nullptr;
        next = vacancy;
    }
    const std::string collapsedGroup = hovered.empty() ? std::string() : m_host.collapsedGroupOf(hovered);
    if (!collapsedGroup.empty()) {
        const Entry *e = m_dropBase.find(collapsedGroup);
        auto edge = e ? dropEdge(point, e->rect) : std::nullopt;
        if (edge) {
            DropTarget t;
            t.kind = DropTarget::Kind::Dock;
            t.spec = tileDropTarget(m_dropBase, collapsedGroup, *edge);
            next = t;
        }
    } else if (hoveredNode && hoveredNode->kind == Node::Kind::Panel) {
        const Rect hoveredWorld = m_dropBase.at(hovered).rect;
        const Rect screen = m_host.panelScreen(hoveredWorld);
        auto bar = tabTarget(hoveredNode, p, screen);
        if (bar) {
            next = bar;
        } else {
            auto edge = floatMove ? overlayEdge(p, screen) : dropEdge(point, hoveredWorld);
            DropTarget t;
            if (edge) {
                t.kind = DropTarget::Kind::Dock;
                t.spec = tileDropTarget(m_dropBase, hovered, *edge);
            } else if (floatMove) {
                t.kind = DropTarget::Kind::Float;
            } else {
                t.kind = DropTarget::Kind::Tab;
                t.panel = hovered;
            }
            next = t;
        }
    }

    // Visible seam gaps override overlapping tile-edge targets; the nearest seam wins.
    const double radius = std::max(m_host.inset(), MinSeamHitWidth / 2);
    double closest = radius;
    if (within)
        for (const auto &kv : m_dropBase) {
            const NodePtr &node = kv.second.node;
            if (node->kind != Node::Kind::Split || m_host.collapsedSplit(node->id))
                continue;
            const Rect group = m_host.toScreen(kv.second.rect);
            if (!inside(p, group))
                continue;
            for (size_t i = 1; i < node->children.size(); ++i) {
                const Rect r = m_host.toScreen(m_dropBase.at(node->children[i]->id).rect);
                const double distance = std::abs(node->axis == Axis::X ? p.x - r.x : p.y - r.y);
                if (distance >= closest)
                    continue;
                auto leaves = leafIds(node);
                if (std::none_of(leaves.begin(), leaves.end(), [&](const auto &l) { return l != m_lifted->id; }))
                    continue;
                closest = distance;
                DropTarget t;
                t.kind = DropTarget::Kind::Dock;
                t.spec = seamTarget(node, int(i));
                next = t;
            }
        }

    // An empty stage is the desktop: its edges dock beside it, its interior floats (or takes the view).
    if (within && stage && !stage->child && closest == radius) {
        const Entry *e = m_dropBase.find(stage->id);
        if (e && inside(point, e->rect)) {
            const Rect s = m_host.panelScreen(e->rect);
            std::pair<Edge, double> distances[] = {
                {Edge::Left, p.x - s.x}, {Edge::Right, s.x + s.w - p.x}, {Edge::Top, p.y - s.y}, {Edge::Bottom, s.y + s.h - p.y}};
            auto nearest = *std::min_element(std::begin(distances), std::end(distances),
                                             [](const auto &a, const auto &b) { return a.second < b.second; });
            const double dimension = (nearest.first == Edge::Left || nearest.first == Edge::Right) ? s.w : s.h;
            const bool nearEdge = nearest.second <= std::min(DesktopSplitMaxPx, dimension * 0.28);
            DropTarget t;
            if (nearEdge) {
                t.kind = DropTarget::Kind::Dock;
                t.spec = tileDropTarget(m_dropBase, stage->id, nearest.first);
            } else if (stageIsDesktop) {
                t.kind = DropTarget::Kind::Float;
            } else {
                t.kind = DropTarget::Kind::Stage;
                t.stage = stage->id;
            }
            next = t;
        }
    }

    // Over the desktop, a floating window's interior takes the view as a tab.
    if (next && next->kind == DropTarget::Kind::Float && stageIsDesktop) {
        std::vector<FloatingPanel> floats;
        for (const auto &f : doc.floating)
            if (f.layer == FloatingLayer::Stage)
                floats.push_back(f);
        std::sort(floats.begin(), floats.end(), [](const auto &a, const auto &b) { return a.z > b.z; });
        for (const auto &f : floats) {
            if (f.panel->id == m_lifted->id)
                continue;
            auto r = m_host.floatWorld(f.panel->id);
            if (!r || !inside(point, *r))
                continue;
            auto screen = last.find(f.panel->id);
            std::optional<DropTarget> bar;
            if (screen != last.end())
                bar = tabTarget(f.panel, p, screen->second);
            if (bar) {
                next = bar;
            } else if (!dropEdge(point, *r)) {
                DropTarget t;
                t.kind = DropTarget::Kind::Tab;
                t.panel = f.panel->id;
                next = t;
            }
            break;
        }
    }

    // The outer frame band inserts around the framed group and takes precedence over everything.
    NodePtr group = m_host.framedNode() ? m_host.framedNode() : doc.root;
    if (group) {
        auto frame = frameDropTarget(group, p, viewport.x, viewport.y, std::max(m_host.inset() * 2, MinSeamHitWidth));
        auto leaves = leafIds(group);
        if (frame && std::any_of(leaves.begin(), leaves.end(), [&](const auto &id) { return id != m_lifted->id; })) {
            DropTarget t;
            t.kind = DropTarget::Kind::Dock;
            t.spec = *frame;
            next = t;
        }
    }

    // A torn-out tab can't regroup with its own host; a floating host falls through to the desktop.
    if (next && next->kind == DropTarget::Kind::Tab && next->panel == m_excludedHost && !m_excludedHost.empty()) {
        bool hostStageFloat = false;
        for (const auto &f : doc.floating)
            if (f.panel->id == m_excludedHost && f.layer == FloatingLayer::Stage)
                hostStageFloat = true;
        if (hostStageFloat) {
            DropTarget t;
            t.kind = DropTarget::Kind::Float;
            next = t;
        } else {
            next.reset();
        }
    }
    // Moving an overlay float over empty space, or a stage float over the stage, keeps it floating.
    if (!next && within && (overlayMove || stageMove)) {
        DropTarget t;
        t.kind = DropTarget::Kind::Float;
        next = t;
    }
    if (within && m_floatFirst != m_forceFloat) {
        // Float-first docks only at screen edges and corners; the force modifier floats in tiling-first.
        next = m_floatFirst ? edgeDock() : std::nullopt;
        if (!next && vacancy) {
            next = vacancy;
        } else if (!next) {
            DropTarget t;
            t.kind = DropTarget::Kind::Float;
            next = t;
        }
    }
    // Past the usable area but still on screen (under a panel) a float stays a float; nothing docks there.
    if (!next && !within && layer == FloatingLayer::Overlay && (overlayMove || stageMove || m_floatFirst != m_forceFloat)) {
        const FloatMargin fm = m_host.floatMargin();
        if (p.x >= -fm.left * viewport.x && p.x <= (1 + fm.right) * viewport.x && p.y >= -fm.top * viewport.y && p.y <= (1 + fm.bottom) * viewport.y) {
            DropTarget t;
            t.kind = DropTarget::Kind::Float;
            next = t;
        }
    }
    settle(filter(next), m_now);
}

std::optional<DropTarget> DragSession::filter(const std::optional<DropTarget> &next) const
{
    if (!next)
        return std::nullopt;
    const auto &views = m_lifted->views;
    const auto layer = m_host.floatingLayer();
    using K = DropTarget::Kind;
    if (next->kind == K::Float)
        return layer && m_host.allowed(views, Region::Floating) ? next : std::nullopt;
    if (next->kind == K::Stage)
        return m_host.allowed(views, Region::Stage) ? next : std::nullopt;
    const LayoutDocument &doc = m_pdoc;
    NodePtr stage = findStage(doc.root);
    auto inStage = [&](const std::string &id) {
        return stage && stage->child && id != stage->id && containsNode(stage->child, id);
    };
    if (next->kind == K::Tab && isVacant(findNode(doc.root, next->panel)))
        return m_host.allowed(views, Region::Side) ? next : std::nullopt;
    if (next->kind == K::Tab) {
        if (!m_host.tabsAllowed())
            return std::nullopt;
        bool isFloat = false;
        for (const auto &f : doc.floating)
            if (f.panel->id == next->panel)
                isFloat = true;
        Region r = isFloat ? Region::Floating : inStage(next->panel) ? Region::Stage : Region::Side;
        return m_host.allowed(views, r) ? next : std::nullopt;
    }
    const auto &spec = next->spec;
    Region region;
    if (spec.seam) {
        bool all = !spec.seam->leaves.empty();
        for (const auto &id : spec.seam->leaves)
            all = all && inStage(id);
        region = all && inStage(spec.id) ? Region::Stage : Region::Side;
    } else {
        region = inStage(spec.id) ? Region::Stage : Region::Side;
    }
    return m_host.allowed(views, region) ? next : std::nullopt;
}

void DragSession::settle(const std::optional<DropTarget> &next, double nowMs)
{
    if (sameTarget(next, m_candidate))
        return;
    m_candidate = next;
    m_targetDeadline.reset();
    if (sameTarget(next, m_target))
        return;
    m_targetDeadline = nowMs + SettleMs;
}

void DragSession::tick(double nowMs)
{
    m_now = nowMs;
    if (m_targetDeadline && nowMs >= *m_targetDeadline) {
        m_targetDeadline.reset();
        apply(m_candidate, nowMs);
    }
}

std::optional<Rect> DragSession::worldOf(const std::string &id) const
{
    auto it = m_host.lastRects().find(id);
    if (it == m_host.lastRects().end())
        return std::nullopt;
    return m_host.fromScreenRect(it->second);
}

std::optional<Rect> DragSession::currentWorld(const std::string &id) const
{
    if (auto own = worldOf(id))
        return own;
    NodePtr node = findNode(m_pdoc.root, id);
    std::vector<Rect> rects;
    if (node)
        for (const auto &leaf : leafIds(node))
            if (auto r = worldOf(leaf))
                rects.push_back(*r);
    if (rects.empty()) {
        const Entry *e = m_dropBase.find(id);
        return e ? std::optional<Rect>(e->rect) : std::nullopt;
    }
    return unionOf(rects);
}

Rect DragSession::seamOrigin(const Entries &preview, const Rect &slot) const
{
    const Entry *slotEntry = preview.find(DROP_SLOT);
    const Entry *parentEntry = slotEntry && !slotEntry->parent.empty() ? preview.find(slotEntry->parent) : nullptr;
    NodePtr parent = parentEntry ? parentEntry->node : nullptr;
    Rect collapsedAtCenter = (parent && parent->kind == Node::Kind::Split && parent->axis == Axis::Y)
        ? Rect{slot.x, slot.y + slot.h / 2, slot.w, 0}
        : Rect{slot.x + slot.w / 2, slot.y, 0, slot.h};
    if (!parent || parent->kind != Node::Kind::Split)
        return collapsedAtCenter;
    int index = -1;
    for (size_t i = 0; i < parent->children.size(); ++i)
        if (parent->children[i]->id == DROP_SLOT)
            index = int(i);
    const NodePtr after = index + 1 < int(parent->children.size()) ? parent->children[index + 1] : nullptr;
    const NodePtr before = index > 0 ? parent->children[index - 1] : nullptr;
    std::optional<Rect> neighbour = after ? currentWorld(after->id) : before ? currentWorld(before->id) : std::nullopt;
    if (!neighbour)
        return collapsedAtCenter;
    if (parent->axis == Axis::X) {
        Rect r = slot;
        r.x = after ? neighbour->x : neighbour->x + neighbour->w;
        r.w = 0;
        return r;
    }
    Rect r = slot;
    r.y = after ? neighbour->y : neighbour->y + neighbour->h;
    r.h = 0;
    return r;
}

Rect DragSession::collapsedSource() const
{
    const auto metrics = m_host.layoutMetrics();
    const Entries entries = layoutRects(m_origin.root, &metrics);
    const Entry *entry = entries.find(m_lifted->id);
    Rect rect;
    auto baseIt = m_base.find(m_lifted->id);
    if (baseIt != m_base.end())
        rect = baseIt->second;
    else if (entry)
        rect = entry->rect;
    const Entry *parentEntry = entry && !entry->parent.empty() ? entries.find(entry->parent) : nullptr;
    NodePtr parent = parentEntry ? parentEntry->node : nullptr;
    if (!parent || parent->kind != Node::Kind::Split) {
        rect.w = 0;
        return rect;
    }
    int index = 0;
    for (size_t i = 0; i < parent->children.size(); ++i)
        if (parent->children[i]->id == m_lifted->id)
            index = int(i);
    const bool hasNext = index < int(parent->children.size()) - 1;
    if (parent->axis == Axis::X) {
        if (!hasNext)
            rect.x += rect.w;
        rect.w = 0;
    } else {
        if (!hasNext)
            rect.y += rect.h;
        rect.h = 0;
    }
    return rect;
}

RectMap DragSession::restingLayout() const
{
    RectMap rects = m_base;
    for (const auto &kv : m_dropBase)
        rects[kv.first] = kv.second.rect;
    if (m_collapsed)
        rects[SOURCE_SLOT] = collapsedSource();
    else if (m_inDoc && !m_fromFloat && m_base.count(m_lifted->id))
        rects[SOURCE_SLOT] = m_base.at(m_lifted->id);
    return rects;
}

bool DragSession::leavesVacancy(const DropTarget &target) const
{
    if (!m_floatFirst || !m_inDoc || m_fromFloat)
        return false;
    if (target.kind == DropTarget::Kind::Float)
        return true;
    return target.kind == DropTarget::Kind::Tab && isVacant(findNode(m_host.doc().root, target.panel));
}

void DragSession::apply(const std::optional<DropTarget> &next, double nowMs)
{
    if (sameTarget(next, m_target))
        return;
    RectMap previous = m_host.lastRects();
    const auto metrics = m_host.layoutMetrics();
    const Entries originEntries = layoutRects(m_origin.root, &metrics);
    const Entry *originEntry = originEntries.find(m_lifted->id);
    const bool hasParent = originEntry && !originEntry->parent.empty();
    // Float-first leaves a hole where the window was instead of reflowing the neighbours, for a float or a vacancy fill.
    const bool leavesHole = next && leavesVacancy(*next);
    if (next && m_inDoc && !m_fromFloat && !m_collapsed && hasParent && !leavesHole) {
        // Latch removal on the first target; leaving it never reopens the source.
        m_collapsed = true;
        m_pdoc = removePanel(m_pdoc, m_lifted->id, true);
        m_dropBase = layoutRects(m_pdoc.root, &metrics);
    }
    m_layoutTargets = restingLayout();
    m_dropLabel.clear();
    auto setSlotFrom = [&](const Rect &world) { previous[DROP_SLOT] = m_host.panelScreen(world); };
    using K = DropTarget::Kind;
    if (next && next->kind == K::Tab) {
        std::optional<Rect> world;
        if (const Entry *e = m_dropBase.find(next->panel))
            world = e->rect;
        else if (auto fw = m_host.floatWorld(next->panel))
            world = fw;
        else
            world = worldOf(next->panel);
        if (world) {
            m_layoutTargets[DROP_SLOT] = *world;
            setSlotFrom(*world);
        } else {
            auto it = m_host.lastRects().find(next->panel);
            if (it != m_host.lastRects().end()) {
                if (auto w = m_host.fromScreenRect(it->second))
                    m_layoutTargets[DROP_SLOT] = *w;
                previous[DROP_SLOT] = it->second;
            }
        }
        m_dropLabel = "Add as tab";
    } else if (next && next->kind == K::Stage) {
        const Rect world = m_dropBase.at(next->stage).rect;
        m_layoutTargets[DROP_SLOT] = world;
        setSlotFrom({world.x + world.w / 2, world.y + world.h / 2, 0, 0});
    } else if (next && next->kind == K::Dock) {
        NodePtr placeholder = makePanel(DROP_SLOT, {DROP_SLOT}, DROP_SLOT);
        const auto &spec = next->spec;
        if (spec.seam || !m_dropBase.has(spec.id)) {
            NodePtr previewRoot = applyDockTarget(m_pdoc.root, spec, placeholder, "__trellis-seam-preview");
            // Back in its old slot: preview the original sizes, with the slot standing for the panel.
            if (m_inDoc && !m_fromFloat && sameShape(m_origin.root, previewRoot, DROP_SLOT, m_lifted->id))
                previewRoot = renameNode(m_origin.root, m_lifted->id, DROP_SLOT);
            const Entries preview = layoutRects(previewRoot, &metrics);
            for (const auto &kv : preview)
                m_layoutTargets[kv.first] = kv.second.rect;
            if (const Entry *slot = preview.find(DROP_SLOT))
                setSlotFrom(seamOrigin(preview, slot->rect));
        } else {
            const DropRects r = dropRects(m_dropBase.at(spec.id).rect, spec.edge);
            m_layoutTargets[spec.id] = r.remaining;
            m_layoutTargets[DROP_SLOT] = r.slot;
            // Grow from the target's edge where it is now, even if the target is still moving.
            auto current = currentWorld(spec.id);
            setSlotFrom(current ? dropRects(*current, spec.edge).collapsed : r.collapsed);
        }
    } else if (!next && m_target && m_target->kind != K::Float) {
        // Leaving a target for nowhere collapses its slot; moving onto the desktop (a float) just
        // lets it fade where it is, since a desktop drop reserves no slot.
        auto it = previous.find(DROP_SLOT);
        std::optional<Rect> world;
        if (it != previous.end())
            world = m_host.fromScreenRect(it->second);
        if (world) {
            const Edge edge = m_target->kind == K::Dock ? m_target->spec.edge : Edge::Left;
            m_layoutTargets[DROP_SLOT] = (edge == Edge::Left || edge == Edge::Right)
                ? Rect{world->x + world->w / 2, world->y, 0, world->h}
                : Rect{world->x, world->y + world->h / 2, world->w, 0};
        }
    }
    if (m_collapsed && !previous.count(SOURCE_SLOT)) {
        auto b = m_base.find(m_lifted->id);
        if (b != m_base.end())
            previous[SOURCE_SLOT] = m_host.panelScreen(b->second);
    }
    m_target = next;
    if (!m_host.reduced())
        m_host.beginTween(previous, nowMs);
}

Rect DragSession::liftedRect(double nowMs)
{
    const Rect original = m_sourceScreen;
    NodePtr stage = findStage(m_pdoc.root);
    std::optional<Rect> desktop;
    if (stage && !stage->child && m_host.floatingLayer() == FloatingLayer::Stage)
        if (const Entry *e = m_dropBase.find(stage->id))
            desktop = m_host.panelScreen(e->rect);
    const Point vp = m_host.viewport();
    std::optional<Rect> source;
    if (!m_collapsed) {
        if (m_tabOrigin)
            source = m_tabOrigin;
        else if (m_fromFloat && !m_fromFrame)
            source = desktop ? desktop : (*m_fromFloat == FloatingLayer::Overlay ? Rect{0, 0, vp.x, vp.y} : original);
        else
            source = original;
    }
    const double amount = m_host.reduced() ? 1 : 1 - std::exp(-(nowMs - m_sizeTick) / 60);
    m_sizeTick = nowMs;
    m_desktopAmount += ((desktop && inside(m_pointer, *desktop) ? 1 : 0) - m_desktopAmount) * amount;
    const double compactTo =
        ((source && inside(m_pointer, *source)) || (!m_fromFrame && desktop && inside(m_pointer, *desktop))) ? 0 : 1;
    if (compactTo != m_compactTo) {
        m_compactFrom = m_compactAmount;
        m_compactTo = compactTo;
        m_pickupAt = nowMs;
    }
    const double progress = m_host.reduced() ? 1 : std::min(1.0, (nowMs - m_pickupAt) / PickupMs);
    const double eased = 1 - std::pow(1 - progress, 3);
    m_compactAmount = m_compactFrom + (m_compactTo - m_compactFrom) * eased;
    const double targetScale = std::min({1.0, Compact.x / std::max(1.0, original.w), Compact.y / std::max(1.0, original.h)});
    const double scale = 1 + (targetScale - 1) * m_compactAmount;
    double w = m_fromFrame ? original.w + (Compact.x - original.w) * m_compactAmount : original.w * scale;
    double h = m_fromFrame ? original.h + (Compact.y - original.h) * m_compactAmount : original.h * scale;
    const Point min = m_host.minSize(m_lifted->views);
    const double bar = m_host.tabbarHeight(*m_lifted);
    w += (std::max(w, min.x + m_host.inset() * 2) - w) * m_desktopAmount;
    h += (std::max(h, min.y + bar + m_host.inset() * 2) - h) * m_desktopAmount;
    const double grabY = m_grabY + (std::min(m_grabY, 18.0) - m_grabY) * m_compactAmount;
    m_floating = {m_pointer.x - w * m_grabX, m_pointer.y - grabY, w, h};
    // A card on its way to becoming an overlay float stays inside the usable area, as the float will.
    if (m_candidate && m_candidate->kind == DropTarget::Kind::Float && m_host.floatingLayer() == FloatingLayer::Overlay) {
        const FloatMargin m = m_host.floatMargin();
        const double minX = -m.left * vp.x, maxX = (1 + m.right) * vp.x, minY = -m.top * vp.y, maxY = (1 + m.bottom) * vp.y;
        m_floating.w = std::min(m_floating.w, maxX - minX);
        m_floating.h = std::min(m_floating.h, maxY - minY);
        m_floating.x = std::clamp(m_floating.x, minX, maxX - m_floating.w);
        m_floating.y = std::clamp(m_floating.y, minY, maxY - m_floating.h);
    }
    return m_floating;
}

namespace {

double area(const Rect &r) { return std::max(0.0, r.w) * std::max(0.0, r.h); }

bool overlaps(const Rect &a, const Rect &b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

/// Shrinks `zone` until it clears every obstacle, each time keeping the largest piece that still holds `anchor`.
Rect clearOf(Rect zone, Point anchor, const std::vector<Rect> &obstacles)
{
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (const Rect &o : obstacles) {
            if (!overlaps(zone, o))
                continue;
            const Rect pieces[] = {{zone.x, zone.y, o.x - zone.x, zone.h},
                                   {o.x + o.w, zone.y, zone.x + zone.w - (o.x + o.w), zone.h},
                                   {zone.x, zone.y, zone.w, o.y - zone.y},
                                   {zone.x, o.y + o.h, zone.w, zone.y + zone.h - (o.y + o.h)}};
            const Rect *best = nullptr;
            for (const Rect &piece : pieces)
                if (piece.w > 0 && piece.h > 0 && inside(anchor, piece) && (!best || area(piece) > area(*best)))
                    best = &piece;
            if (!best)
                continue; // the pointer is over this one; it gets covered
            zone = *best;
            changed = true;
        }
        if (!changed)
            break;
    }
    return zone;
}

} // namespace

std::optional<Rect> DragSession::snapZone() const
{
    if (!m_target || m_target->kind != DropTarget::Kind::Float)
        return std::nullopt;
    const FloatingLayer layer = m_fromFloat == FloatingLayer::Overlay ? FloatingLayer::Overlay : m_host.floatingLayer().value_or(FloatingLayer::Overlay);
    if (layer != FloatingLayer::Overlay)
        return std::nullopt;
    const Point vp = m_host.viewport();
    const Point p = m_pointer;
    if (p.x < 0 || p.y < 0 || p.x > vp.x || p.y > vp.y)
        return std::nullopt;
    const double right = vp.x - p.x, bottom = vp.y - p.y;
    if (p.x > SnapEdgePx && right > SnapEdgePx && p.y > SnapEdgePx && bottom > SnapEdgePx)
        return std::nullopt;
    // One horizontal and one vertical side, each unambiguous, make a corner.
    const bool cl = p.x <= SnapCornerPx, cr = right <= SnapCornerPx, ct = p.y <= SnapCornerPx, cb = bottom <= SnapCornerPx;
    const bool corner = (cl != cr) && (ct != cb);
    const bool l = corner ? cl : p.x <= SnapEdgePx, r = corner ? cr : right <= SnapEdgePx;
    const bool t = corner ? ct : p.y <= SnapEdgePx, b = corner ? cb : bottom <= SnapEdgePx;
    // Half toward the edge, a quarter in a corner.
    Rect z{0, 0, vp.x, vp.y};
    if (l != r) {
        z.w = vp.x / 2;
        if (r)
            z.x = vp.x / 2;
    }
    if (t != b) {
        z.h = vp.y / 2;
        if (b)
            z.y = vp.y / 2;
    }
    // Other floats in the way: instead of covering them, fill the room left around them. An edge may
    // grow past its half; a corner stays within its quarter.
    std::vector<Rect> others;
    for (const auto &f : m_pdoc.floating) {
        if (f.layer != FloatingLayer::Overlay || f.panel->id == m_lifted->id)
            continue;
        auto it = m_host.lastRects().find(f.panel->id);
        if (it != m_host.lastRects().end())
            others.push_back(it->second);
    }
    if (std::none_of(others.begin(), others.end(), [&](const Rect &o) { return overlaps(z, o); }))
        return z;
    const Rect room = corner ? z : Rect{0, 0, vp.x, vp.y};
    const Point anchor{std::clamp(p.x, z.x, z.x + z.w), std::clamp(p.y, z.y, z.y + z.h)};
    Rect fit = clearOf(room, anchor, others);
    if (!corner) {
        // Stay on the side of the screen the pointer is at the edge of: the piece must touch that edge.
        if (l && fit.x > 0) fit = z;
        if (r && fit.x + fit.w < vp.x) fit = z;
        if (t && fit.y > 0) fit = z;
        if (b && fit.y + fit.h < vp.y) fit = z;
    }
    // A sliver squeezed between floats is no use: take the half or quarter and overlap them instead.
    return (fit.w >= z.w * 0.5 && fit.h >= z.h * 0.5) ? fit : z;
}

Rect DragSession::floatRect(const LayoutDocument &next, FloatingLayer layer) const
{
    const Point viewport = m_host.viewport();
    Rect container;
    if (layer == FloatingLayer::Overlay) {
        if (auto snap = snapZone())
            return {snap->x / viewport.x, snap->y / viewport.y, snap->w / viewport.x, snap->h / viewport.y};
        container = {0, 0, viewport.x, viewport.y};
    } else {
        NodePtr stage = findStage(next.root);
        Rect world = UNIT;
        if (stage) {
            const auto metrics = m_host.layoutMetrics();
            if (const Entry *e = layoutRects(next.root, &metrics).find(stage->id))
                world = e->rect;
        }
        container = m_host.toScreen(world);
    }
    Rect original;
    if (m_fromFloat && !m_tabRollback) {
        auto it = m_host.lastRects().find(m_lifted->id);
        original = m_fromFrame ? m_floating : (it != m_host.lastRects().end() ? it->second : m_sourceScreen);
    } else {
        original = m_fromFrame ? m_floating : m_sourceScreen;
    }
    const Point min = m_host.minSize(m_lifted->views);
    const double bar = m_host.tabbarHeight(*m_lifted);
    double w, h;
    if (m_fromFloat && !m_fromFrame && !m_tabRollback) {
        w = original.w;
        h = original.h;
    } else {
        w = std::min(container.w * (2.0 / 3), original.w);
        h = std::min(container.h * (2.0 / 3), original.h);
    }
    // The unscaled content minimum wins over the drop cap.
    w = std::max(w, min.x + m_host.inset() * 2);
    h = std::max(h, min.y + bar + m_host.inset() * 2);
    const double grabY = std::min(m_grabY, 18 + (m_grabY - 18) * (1 - m_compactAmount));
    const double x = m_pointer.x - w * m_grabX;
    const double y = m_pointer.y - grabY;
    Rect r{(x - container.x) / container.w, (y - container.y) / container.h, w / container.w, h / container.h};
    if (layer == FloatingLayer::Overlay) {
        // An overlay float stays inside the usable area, so it can reach under a panel but never leaves the screen.
        const FloatMargin m = next.floatMargin;
        const double minX = -m.left, maxX = 1 + m.right, minY = -m.top, maxY = 1 + m.bottom;
        r.w = std::min(r.w, maxX - minX);
        r.h = std::min(r.h, maxY - minY);
        r.x = std::clamp(r.x, minX, maxX - r.w);
        r.y = std::clamp(r.y, minY, maxY - r.h);
    }
    return r;
}

DragOutcome DragSession::end(bool commit, double nowMs)
{
    DragOutcome out;
    out.liftedId = m_lifted ? m_lifted->id : std::string();
    m_targetDeadline.reset();
    // Release expresses intent immediately, even before the hover delay expires.
    if (commit && m_active)
        apply(m_candidate, nowMs);
    if (!m_active)
        return out; // a click, or a finished tab sort: the shell handles it
    out.wasDrag = true;
    out.from = m_host.lastRects();
    out.from[m_lifted->id] = m_floating;
    out.fromFloating = m_fromFloat.has_value() && !m_tabRollback;
    const std::optional<DropTarget> target = commit ? m_target : std::nullopt;
    if (!target) {
        // Cancel or no target: restore the original arrangement; the window flies back.
        m_host.setDoc(m_origin);
        out.next = m_origin;
        out.settle = m_lifted->id;
        return out;
    }
    const LayoutDocument base = m_host.doc();
    LayoutDocument without = m_inDoc ? removePanel(base, m_lifted->id, true) : base;
    if (leavesVacancy(*target))
        if (auto vacated = vacatePanel(base, m_lifted->id))
            without = *vacated;
    LayoutDocument next;
    const FloatingLayer layer = m_host.floatingLayer().value_or(FloatingLayer::Overlay);
    using K = DropTarget::Kind;
    if (target->kind == K::Tab && isVacant(findNode(without.root, target->panel))) {
        next = fillVacancy(without, target->panel, m_lifted);
    } else if (target->kind == K::Tab) {
        next = insertPanel(without, m_lifted, DockTarget::intoPanel(target->panel, target->index));
    } else if (target->kind == K::Stage) {
        next = insertPanel(without, m_lifted, DockTarget::intoPanel(target->stage));
    } else if (target->kind == K::Dock) {
        next = without;
        next.root = applyDockTarget(without.root, target->spec, m_lifted, uid("split"));
        // Dropping a window back where it came from changes nothing, so keep its original sizes.
        if (m_inDoc && !m_fromFloat && sameShape(m_origin.root, next.root, m_lifted->id, m_lifted->id))
            next.root = m_origin.root;
    } else {
        const FloatingLayer floatLayer = m_fromFloat == FloatingLayer::Overlay ? FloatingLayer::Overlay : layer;
        next = floatPanel(without, m_lifted, floatRect(without, floatLayer), floatLayer);
    }
    const bool repositioning = target->kind == K::Float && m_fromFloat && !m_tabRollback;
    m_host.setDoc(m_origin);
    out.committed = true;
    out.next = next;
    out.target = target->kind;
    out.settle = (repositioning || target->kind == K::Tab) ? std::string() : m_lifted->id;
    return out;
}

} // namespace trellis
