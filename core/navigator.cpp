#include "navigator.h"

#include "document.h"

#include <algorithm>
#include <cmath>

namespace trellis {

constexpr int MAX_HISTORY = 100;

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

void Navigator::sync()
{
    const auto metrics = m_host.layoutMetrics();
    m_entries = focusLayout(m_host.doc().root, &metrics);
    if (!m_framed.empty() && !m_entries.has(m_framed)) {
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
    retarget();
}

void Navigator::retarget()
{
    const Rect target = rectOf(m_framed);
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
    const bool changed = m_framed != id;
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
    if (m_framed.empty())
        return;
    focus(navParent(m_entries, m_framed));
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
    if (!m_framed.empty())
        m_overviewReturn = m_framedLeaves;
    focus({});
}

void Navigator::toggleOverview()
{
    if (m_framed.empty() && m_overviewReturn) {
        auto leaves = *m_overviewReturn;
        m_overviewReturn.reset();
        focus(savedFrameDestination(m_host.doc().root, leaves));
    } else if (!m_framed.empty()) {
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

void Navigator::ensureVisible(const std::string &panelId)
{
    if (m_framed.empty())
        return;
    const auto &doc = m_host.doc();
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
    if (containsNode(node, panelId))
        return;
    auto ids = m_framedLeaves;
    ids.push_back(panelId);
    focus(frameLeaves(doc.root, ids));
}

void Navigator::include(const std::vector<std::string> &ids)
{
    if (m_framed.empty())
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
    double minSize = std::numeric_limits<double>::infinity();
    for (const auto &kv : m_entries)
        minSize = std::min(minSize, std::min(kv.second.rect.w, kv.second.rect.h));
    minSize *= 0.65;
    factor = std::max(minSize / std::min(c.w, c.h), std::min(factor, 1.35 / std::max(c.w, c.h)));
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
    if (nowMs - m_wheel.last > 260 || direction != m_wheel.direction)
        resetStepWheel();
    m_wheel.last = nowMs;
    m_wheel.direction = direction;
    m_wheel.total += delta;
    if (nowMs - m_wheel.steppedAt >= 140 && std::abs(m_wheel.total) >= 12) {
        m_wheel.steppedAt = nowMs;
        const bool out = m_wheel.total > 0;
        m_wheel.total = 0;
        focus(hierarchyStep(m_entries, m_framed.empty() ? rootId() : m_framed, out ? Direction::Out : Direction::In,
                            worldPoint));
        return true;
    }
    return false;
}

Navigation Navigator::serialize() const
{
    Navigation nav;
    if (!m_framed.empty())
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
    if (!destination.empty() && on())
        focus(destination);
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
    f.frame = m_framed.empty() ? leafIds(m_host.doc().root) : m_framedLeaves;
    m_framings.push_back(f);
    m_host.changed();
    return f;
}

void Navigator::goFraming(const std::string &id)
{
    for (const auto &f : m_framings)
        if (f.id == id)
            return focus(savedFrameDestination(m_host.doc().root, f.frame));
}

void Navigator::removeFraming(const std::string &id)
{
    m_framings.erase(std::remove_if(m_framings.begin(), m_framings.end(), [&](const Framing &f) { return f.id == id; }),
                     m_framings.end());
    m_host.changed();
}

} // namespace trellis
