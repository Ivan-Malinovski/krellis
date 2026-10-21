#include "navigator.h"

#include "document.h"

#include <algorithm>
#include <cmath>

namespace trellis {

constexpr int MAX_HISTORY = 100;
constexpr double WHEEL_QUIET_MS = 180; // a pause this long ends a scroll gesture
constexpr double GRID_EPS = 1e-6;
constexpr double WIDEN_LIMIT = 2.2; // a new tile widens the frame when that at most doubles its area
constexpr double ZOOM_OVERSHOOT = 0.12; // the rubber band stops this far (log scale, ~13%) past a zoom limit

namespace {
struct Leaf {
    std::string id;
    Rect r;
};
bool inside(const Rect &outer, const Rect &r)
{
    return r.x >= outer.x - GRID_EPS && r.y >= outer.y - GRID_EPS && r.x + r.w <= outer.x + outer.w + GRID_EPS
        && r.y + r.h <= outer.y + outer.h + GRID_EPS;
}
bool overlaps(const Rect &a, const Rect &b)
{
    return a.x < b.x + b.w - GRID_EPS && b.x < a.x + a.w - GRID_EPS && a.y < b.y + b.h - GRID_EPS
        && b.y < a.y + a.h - GRID_EPS;
}
Rect unite(const Rect &a, const Rect &b)
{
    const double x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.x + a.w, b.x + b.w) - x, std::max(a.y + a.h, b.y + b.h) - y};
}
double lo(const Rect &r, int a) { return a == 0 ? r.x : r.y; }
double hi(const Rect &r, int a) { return a == 0 ? r.x + r.w : r.y + r.h; }
void setSpan(Rect &r, int a, double l, double h)
{
    (a == 0 ? r.x : r.y) = l;
    (a == 0 ? r.w : r.h) = h - l;
}
double distanceTo(const Rect &r, Point p)
{
    return std::hypot(std::max({r.x - p.x, 0.0, p.x - (r.x + r.w)}), std::max({r.y - p.y, 0.0, p.y - (r.y + r.h)}));
}
/// A zoom push `f` against the limits, as a rubber band. `inRatio` is how far the camera is below its smallest
/// size and `outRatio` how far above its largest (a value over 1 means past the limit). Inside the limits a push
/// goes through unchanged; past one, the camera's overshoot o (log scale) is the compressed image of a raw pull r,
/// o = M (1 - exp(-r / M)), so it can never get further than M, and pulling back retraces the same curve.
double bandedFactor(double f, double inRatio, double outRatio)
{
    const double M = ZOOM_OVERSHOOT;
    const double push = std::log(f);
    const auto band = [&](double x, double d) { // x: overshoot (< 0 inside the limit), d: the push toward it
        const double xc = std::min(x, M * (1 - 1e-9));
        const double raw = (xc > 0 ? -M * std::log(1 - xc / M) : xc) + d;
        return (raw > 0 ? M * (1 - std::exp(-raw / M)) : raw) - x;
    };
    const double xIn = std::log(inRatio), xOut = std::log(outRatio);
    if (xIn > 0 || (xOut <= 0 && push < 0))
        return std::exp(-band(xIn, -push));
    return std::exp(band(xOut, push));
}
} // namespace

std::string Navigator::rootId() const
{
    const auto &doc = m_host.doc();
    return doc.root ? doc.root->id : std::string();
}

Rect Navigator::rectOf(const std::string &id) const
{
    if (!id.empty())
        if (const Entry *e = m_entries.find(id))
            return e->rect;
    return UNIT;
}

NodePtr Navigator::framedNode() const
{
    if (m_framed.empty())
        return nullptr;
    const Entry *e = m_entries.find(m_framed);
    return e ? e->node : nullptr;
}

Rect Navigator::framedRect() const
{
    if (!m_boxed)
        return rectOf(m_framed);
    std::optional<Rect> box;
    for (const auto &id : m_framedLeaves)
        if (const Entry *e = m_entries.find(id))
            box = box ? unite(*box, e->rect) : e->rect;
    return box.value_or(rectOf(m_framed));
}

void Navigator::sync()
{
    const auto metrics = m_host.layoutMetrics();
    m_entries = focusLayout(m_host.doc().root, &metrics);
    if (m_boxed) {
        std::vector<std::string> surviving;
        for (const auto &id : m_framedLeaves)
            if (m_entries.has(id))
                surviving.push_back(id);
        m_framedLeaves = surviving;
        m_boxed = !surviving.empty();
        if (m_boxed) {
            const std::string next = frameLeaves(m_host.doc().root, surviving);
            m_framed = next != rootId() ? next : std::string();
            auto whole = leafIds(m_framed.empty() ? m_host.doc().root : m_entries.at(m_framed).node);
            std::sort(whole.begin(), whole.end());
            std::sort(surviving.begin(), surviving.end());
            m_boxed = whole != surviving;
            if (!m_boxed)
                m_framedLeaves = m_framed.empty() ? std::vector<std::string>{} : whole;
        } else {
            m_framed.clear();
        }
    }
    if (m_boxed) {
        // Keep the box; its node and tiles are already current.
    } else if (!m_framed.empty() && !m_entries.has(m_framed)) {
        std::vector<std::string> surviving;
        for (const auto &id : m_framedLeaves)
            if (m_entries.has(id))
                surviving.push_back(id);
        std::string next = surviving.empty() ? std::string() : frameLeaves(m_host.doc().root, surviving);
        m_framed = (!next.empty() && next != rootId()) ? next : std::string();
        m_framedLeaves = m_framed.empty() ? std::vector<std::string>{} : leafIds(m_entries.at(m_framed).node);
    } else if (!m_framed.empty()) {
        m_framedLeaves = leafIds(m_entries.at(m_framed).node);
    }
    if (m_session && !m_entries.has(m_session->id))
        m_session.reset();
    m_minTile = std::numeric_limits<double>::infinity();
    for (const auto &kv : m_entries)
        m_minTile = std::min(m_minTile, std::min(kv.second.rect.w, kv.second.rect.h));
    retarget();
}

Rect Navigator::cameraFor(const Rect &frame) const
{
    const Point vp = m_host.viewport();
    const double px = m_host.peekPadding();
    if (px <= 0 || overviewing() || vp.x <= 0 || vp.y <= 0)
        return frame;
    const Rect root = rectOf(rootId());
    constexpr double EPS = 1e-6, MAX_SHARE = 0.06;
    Rect out = frame;
    const auto widen = [&](double &lo, double &size, double rootLo, double rootSize, double view) {
        const double share = std::min(px / view, MAX_SHARE);
        const double before = lo > rootLo + EPS ? share : 0, after = lo + size < rootLo + rootSize - EPS ? share : 0;
        const double whole = size / (1 - before - after);
        lo -= before * whole;
        size = whole;
    };
    widen(out.x, out.w, root.x, root.w, vp.x);
    widen(out.y, out.h, root.y, root.h, vp.y);
    return out;
}

void Navigator::retarget()
{
    const Rect target = cameraFor(framedRect());
    if (!sameRect(m_camera.target, target)) {
        m_camera.target = target;
        if (m_host.reducedMotion())
            m_camera.finish();
        m_host.schedule();
    }
}

void Navigator::focus(std::string id, bool record)
{
    if (!on())
        id.clear();
    const auto &doc = m_host.doc();
    if (!id.empty()) {
        for (const auto &f : doc.floating)
            if (f.panel->id == id) {
                auto stage = findStage(doc.root);
                id = stage ? stage->id : std::string();
                break;
            }
    }
    if (!id.empty() && !m_entries.has(id))
        id.clear();
    if (!id.empty())
        id = navNode(m_entries, id);
    if (id == rootId())
        id.clear();
    if (m_session && m_session->id != id)
        m_session.reset();
    if (record) {
        Visit visit{id.empty() ? rootId() : id, leafIds(id.empty() ? doc.root : m_entries.at(id).node)};
        auto h = recordVisit(m_visits, m_visitIndex, visit);
        m_visits = std::move(h.visits);
        m_visitIndex = h.index;
        if (int(m_visits.size()) > MAX_HISTORY) {
            m_visits.erase(m_visits.begin(), m_visits.end() - MAX_HISTORY);
            m_visitIndex = int(m_visits.size()) - 1;
        }
    }
    endGesture();
    const bool changed = m_framed != id || m_boxed;
    m_boxed = false;
    m_framed = id;
    m_framedLeaves = id.empty() ? std::vector<std::string>{} : leafIds(m_entries.at(id).node);
    retarget();
    if (changed)
        m_host.changed();
}

void Navigator::endGesture()
{
    if (m_gesture) {
        m_gesture = false;
        m_host.gestureChanged(false);
    }
}

bool Navigator::toggle(const std::string &id)
{
    if (!on())
        return false;
    for (const auto &f : m_host.doc().floating)
        if (f.panel->id == id)
            return false;
    if (!m_entries.has(id))
        return false;
    auto next = maximizeTransition(m_entries, rootId(), m_framed.empty() ? rootId() : m_framed, navNode(m_entries, id),
                                   m_session);
    focus(next.destination);
    m_session = next.session;
    return true;
}

void Navigator::stepOut()
{
    if (overviewing())
        return;
    focus(m_boxed ? m_framed : navParent(m_entries, m_framed));
}

void Navigator::stepIn(std::optional<Point> worldPoint)
{
    Point p = worldPoint.value_or(Point{m_camera.value.x + m_camera.value.w / 2, m_camera.value.y + m_camera.value.h / 2});
    focus(hierarchyStep(m_entries, m_framed.empty() ? rootId() : m_framed, Direction::In, p));
}

void Navigator::historyGo(int direction)
{
    const int next = m_visitIndex + direction;
    if (next < 0 || next >= int(m_visits.size()))
        return;
    m_visitIndex = next;
    m_session.reset();
    std::string destination = savedFrameDestination(m_host.doc().root, m_visits[next].leaves);
    if (destination.empty()) {
        auto stage = findStage(m_host.doc().root);
        destination = stage ? stage->id : std::string();
    }
    focus(destination, false);
}

void Navigator::overview()
{
    if (!overviewing())
        m_overviewReturn = m_framedLeaves;
    focus({});
}

void Navigator::toggleOverview()
{
    if (overviewing() && m_overviewReturn) {
        auto leaves = *m_overviewReturn;
        m_overviewReturn.reset();
        frameSet(leaves);
    } else if (!overviewing()) {
        m_overviewReturn = m_framedLeaves;
        focus({});
    }
}

void Navigator::frame(FrameAll which)
{
    if (which == FrameAll::All)
        return focus({});
    auto stage = findStage(m_host.doc().root);
    focus(stage ? stage->id : std::string());
}

void Navigator::frameIds(const std::vector<std::string> &targets)
{
    std::vector<std::string> ids;
    for (const auto &t : targets) {
        if (const Entry *e = m_entries.find(t)) {
            auto l = leafIds(e->node);
            ids.insert(ids.end(), l.begin(), l.end());
        } else {
            // A view id: frame the panel that holds it.
            for (const auto &leaf : leafIds(m_host.doc().root)) {
                const Entry *le = m_entries.find(leaf);
                if (le && le->node->kind == Node::Kind::Panel
                    && std::find(le->node->views.begin(), le->node->views.end(), t) != le->node->views.end()) {
                    ids.push_back(leaf);
                    break;
                }
            }
        }
    }
    if (ids.empty())
        return;
    focus(frameLeaves(m_host.doc().root, ids));
}

void Navigator::reveal(const std::string &panelId, bool preferWiden)
{
    if (overviewing())
        return;
    const auto &doc = m_host.doc();
    if (m_framed.empty()) { // a block inside the whole layout
        if (!m_entries.has(panelId) || std::find(m_framedLeaves.begin(), m_framedLeaves.end(), panelId) != m_framedLeaves.end())
            return;
        slideTo(panelId, preferWiden);
        return;
    }
    const Entry *fe = m_entries.find(m_framed);
    if (!fe)
        return;
    NodePtr node = fe->node;
    for (const auto &f : doc.floating) {
        if (f.panel->id != panelId)
            continue;
        if (f.layer == FloatingLayer::Overlay)
            return;
        auto stage = findStage(doc.root);
        if (stage) {
            auto leaves = leafIds(node);
            if (std::find(leaves.begin(), leaves.end(), stage->id) != leaves.end())
                return;
            if (node->id == stage->id || containsNode(node, stage->id))
                return;
        }
        focus(stage ? stage->id : std::string());
        return;
    }
    if (!m_entries.has(panelId)) // a drawer window or a stale id: nothing to reveal
        return;
    if (m_boxed ? std::find(m_framedLeaves.begin(), m_framedLeaves.end(), panelId) != m_framedLeaves.end()
                : containsNode(node, panelId))
        return;
    slideTo(panelId, preferWiden);
}

void Navigator::slideTo(const std::string &panelId, bool preferWiden)
{
    const Rect box = framedRect();
    const Rect target = m_entries.at(panelId).rect;
    std::vector<std::string> framed;
    for (const auto &id : m_framedLeaves)
        if (m_entries.has(id))
            framed.push_back(id);
    framed.push_back(panelId);
    if (preferWiden) {
        // A tile that just appeared beside the frame belongs in it, as long as that does not blow the frame up.
        const std::string wide = frameLeaves(m_host.doc().root, framed);
        const Rect r = rectOf(wide);
        if (r.w * r.h <= WIDEN_LIMIT * box.w * box.h) {
            focus(wide);
            return;
        }
    }
    // Keep the frame's size and slide it the least distance that covers the tile; stretch it only on an axis
    // where the tile alone is longer than the frame.
    Rect slid = box;
    for (int a = 0; a < 2; ++a) {
        const double size = hi(box, a) - lo(box, a);
        if (hi(target, a) - lo(target, a) > size + GRID_EPS)
            setSpan(slid, a, lo(target, a), hi(target, a));
        else if (lo(target, a) < lo(box, a) - GRID_EPS)
            setSpan(slid, a, lo(target, a), lo(target, a) + size);
        else if (hi(target, a) > hi(box, a) + GRID_EPS)
            setSpan(slid, a, hi(target, a) - size, hi(target, a));
    }
    std::vector<std::string> ids;
    for (const auto &id : leafIds(m_host.doc().root))
        if (const Entry *e = m_entries.find(id))
            if (id == panelId || inside(slid, e->rect))
                ids.push_back(id);
    // An automatic slide is not a visit: cycling through windows must not fill the history.
    frameSet(ids, false);
}

void Navigator::include(const std::vector<std::string> &ids)
{
    if (overviewing())
        return;
    std::vector<std::string> all;
    for (const auto &id : m_framedLeaves)
        if (m_entries.has(id))
            all.push_back(id);
    all.insert(all.end(), ids.begin(), ids.end());
    focus(frameLeaves(m_host.doc().root, all));
}

void Navigator::zoom(double factor, Point p, Point pan)
{
    if (m_entries.empty())
        return;
    if (!m_gesture) {
        m_gesture = true;
        m_host.gestureChanged(true);
    }
    m_camera.velocity = {0, 0, 0, 0};
    const Rect c = m_camera.value;
    const Point vp = m_host.viewport();
    const double minSize = m_minTile * 0.65;
    if (factor != 1.0)
        factor = bandedFactor(factor, minSize / std::min(c.w, c.h), std::max(c.w, c.h) / 1.35);
    Rect next{c.x + c.w * p.x * (1 - factor) - (pan.x / vp.x) * c.w,
              c.y + c.h * p.y * (1 - factor) - (pan.y / vp.y) * c.h, c.w * factor, c.h * factor};
    next.x = std::max(-next.w * 0.3, std::min(1 - next.w * 0.7, next.x));
    next.y = std::max(-next.h * 0.3, std::min(1 - next.h * 0.7, next.y));
    m_camera.jump(next);
    m_host.schedule();
}

std::string Navigator::gestureCandidate() const
{
    return bestFit(m_camera.value, m_entries);
}

void Navigator::finishGesture()
{
    if (!m_gesture)
        return;
    focus(bestFit(m_camera.value, m_entries));
}

void Navigator::resetStepWheel()
{
    m_wheel = {};
}

bool Navigator::wheelStep(double delta, Point worldPoint, double nowMs)
{
    if (m_host.busy())
        return false;
    const int direction = delta > 0 ? 1 : delta < 0 ? -1 : 0;
    if (!direction)
        return false;
    // One step per gesture: a flick of a free-spinning wheel or a touchpad swipe keeps sending events
    // until it dies away, and only the first one steps. A pause or a reversal starts a new gesture.
    const bool fresh = nowMs - m_wheel.last > WHEEL_QUIET_MS || direction != m_wheel.direction;
    m_wheel.last = nowMs;
    m_wheel.direction = direction;
    return fresh && zoomStep(direction > 0, worldPoint);
}

void Navigator::frameSet(const std::vector<std::string> &leaves, bool record)
{
    std::vector<std::string> survivors;
    for (const auto &id : leaves)
        if (m_entries.has(id))
            survivors.push_back(id);
    std::string destination = savedFrameDestination(m_host.doc().root, survivors);
    focus(destination, record);
    if (survivors.empty() || !on())
        return;
    auto whole = leafIds(m_framed.empty() ? m_host.doc().root : m_entries.at(m_framed).node);
    std::sort(whole.begin(), whole.end());
    std::sort(survivors.begin(), survivors.end());
    if (whole == survivors)
        return;
    m_boxed = true;
    m_framedLeaves = survivors;
    retarget();
    m_host.changed();
}

bool Navigator::zoomStep(bool out, Point at)
{
    if (!on() || m_entries.empty())
        return false;
    std::vector<Leaf> leaves;
    for (const auto &id : leafIds(m_host.doc().root))
        if (const Entry *e = m_entries.find(id))
            leaves.push_back({id, e->rect});
    if (leaves.empty())
        return false;
    const Rect box = framedRect();
    const auto within = [&](const Rect &r) {
        std::vector<std::string> ids;
        for (const auto &l : leaves)
            if (inside(r, l.r))
                ids.push_back(l.id);
        return ids;
    };
    // Tile edges, per axis: the "columns" and "rows" a step moves by.
    std::vector<double> edges[2];
    for (const auto &l : leaves)
        for (int a = 0; a < 2; ++a) {
            edges[a].push_back(lo(l.r, a));
            edges[a].push_back(hi(l.r, a));
        }
    const double pointer[2] = {at.x, at.y};
    Rect next = box;

    if (out) {
        for (int a = 0; a < 2; ++a) {
            const double l = lo(next, a), h = hi(next, a);
            double before = -1e18, after = 1e18;
            for (double v : edges[a]) {
                if (v < l - GRID_EPS) before = std::max(before, v);
                if (v > h + GRID_EPS) after = std::min(after, v);
            }
            const bool canLow = before > -1e17, canHigh = after < 1e17;
            if (!canLow && !canHigh)
                continue;
            const bool high = pointer[a] < (l + h) / 2 ? canHigh : !canLow;
            setSpan(next, a, high ? l : before, high ? after : h);
        }
        // A tile the new edge cuts through joins the block whole.
        for (bool grown = true; grown;) {
            grown = false;
            for (const auto &leaf : leaves)
                if (overlaps(next, leaf.r) && !inside(next, leaf.r)) {
                    next = unite(next, leaf.r);
                    grown = true;
                }
        }
    } else {
        const Leaf *under = nullptr;
        double best = 1e18;
        for (const auto &l : leaves) {
            if (!inside(box, l.r))
                continue;
            const double d = distanceTo(l.r, at);
            if (d < best) {
                best = d;
                under = &l;
            }
        }
        if (!under)
            return false;
        for (int a = 0; a < 2; ++a) {
            const double l = lo(next, a), h = hi(next, a);
            double after = 1e18, before = -1e18;
            for (double v : edges[a]) {
                if (v > l + GRID_EPS && v < h - GRID_EPS) {
                    after = std::min(after, v);
                    before = std::max(before, v);
                }
            }
            if (after > 1e17)
                continue;
            const bool dropHighFirst = pointer[a] < (l + h) / 2;
            for (int attempt = 0; attempt < 2; ++attempt) {
                const bool dropHigh = (attempt == 0) == dropHighFirst;
                Rect trial = next;
                setSpan(trial, a, dropHigh ? l : after, dropHigh ? before : h);
                const auto ids = within(trial);
                if (std::find(ids.begin(), ids.end(), under->id) == ids.end())
                    continue;
                // Tiles cut by the new edge are dropped, so the block is what is left whole.
                std::optional<Rect> kept;
                for (const auto &leaf : leaves)
                    if (inside(trial, leaf.r))
                        kept = kept ? unite(*kept, leaf.r) : leaf.r;
                next = *kept;
                break;
            }
        }
    }
    const auto before = within(box);
    auto after = within(next);
    if (after.empty() || after.size() == before.size())
        return false;
    frameSet(after);
    return true;
}

Navigation Navigator::serialize() const
{
    Navigation nav;
    if (!overviewing())
        nav.frame = m_framedLeaves;
    nav.framings = m_framings;
    return nav;
}

void Navigator::restore(const Navigation &nav)
{
    m_framings = nav.framings;
    std::string destination = nav.frame.empty() ? std::string() : savedFrameDestination(m_host.doc().root, nav.frame);
    m_visits = {{rootId(), leafIds(m_host.doc().root)}};
    m_visitIndex = 0;
    m_session.reset();
    m_overviewReturn.reset();
    m_framed.clear();
    m_framedLeaves.clear();
    m_boxed = false;
    if (!destination.empty() && on())
        frameSet(nav.frame);
    else
        retarget();
}

std::optional<Framing> Navigator::saveFraming(const std::string &name)
{
    auto begin = name.find_first_not_of(" \t\n");
    if (begin == std::string::npos)
        return std::nullopt;
    auto end = name.find_last_not_of(" \t\n");
    Framing f;
    f.id = uid("framing");
    f.name = name.substr(begin, end - begin + 1);
    f.frame = overviewing() ? leafIds(m_host.doc().root) : m_framedLeaves;
    m_framings.push_back(f);
    m_host.changed();
    return f;
}

void Navigator::goFraming(const std::string &id)
{
    for (const auto &f : m_framings)
        if (f.id == id)
            return frameSet(f.frame);
}

void Navigator::removeFraming(const std::string &id)
{
    m_framings.erase(std::remove_if(m_framings.begin(), m_framings.end(), [&](const Framing &f) { return f.id == id; }),
                     m_framings.end());
    m_host.changed();
}

} // namespace trellis
