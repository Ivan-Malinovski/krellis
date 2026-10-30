#include "krellis_effect.h"
#include "window.h"
#include "options.h"
#include "effect/effecthandler.h"
#include <algorithm>

namespace KWin {
using namespace krellis;

void KrellisCanvas::startResize(EffectWindow *w)
{
    const auto *tracked = trackedFor(w);
    if (!tracked || !w->window() || m_session || m_resize) return;
    const auto location = locatePanel(m_doc, tracked->panel);
    if (location.where != PanelLocation::Where::Docked && location.where != PanelLocation::Where::Floating) return;
    NativeResize state;
    state.window = w;
    state.before = m_doc;
    state.painted = visualRect(w).value_or(screenRect(tracked->panel));
    state.camera = m_camera.value;
    state.native = w->window()->moveResizeGeometry();
    state.lastNative = state.native;
    state.anchor = w->window()->interactiveMoveResizeAnchor();
    state.floating = location.where == PanelLocation::Where::Floating;
    const auto gravity = w->window()->interactiveMoveResizeGravity();
    const bool left = gravity == Gravity::Left || gravity == Gravity::TopLeft || gravity == Gravity::BottomLeft;
    const bool right = gravity == Gravity::Right || gravity == Gravity::TopRight || gravity == Gravity::BottomRight;
    const bool top = gravity == Gravity::Top || gravity == Gravity::TopLeft || gravity == Gravity::TopRight;
    const bool bottom = gravity == Gravity::Bottom || gravity == Gravity::BottomLeft || gravity == Gravity::BottomRight;
    if (!state.floating) {
        // Follow the panel's ancestry to the closest divider for each dragged edge.
        // Outside edges have no divider and cannot change the canvas extent.
        for (const auto edge : {krellis::Edge::Left, krellis::Edge::Right, krellis::Edge::Top, krellis::Edge::Bottom}) {
            if (!(edge == krellis::Edge::Left ? left : edge == krellis::Edge::Right ? right : edge == krellis::Edge::Top ? top : bottom)) continue;
            std::string child = tracked->panel;
            while (const auto *entry = m_rects.find(child)) {
                if (entry->parent.empty()) break;
                const auto split = findNode(m_doc.root, entry->parent);
                if (!split) break;
                if (split->kind != Node::Kind::Split) { child = split->id; continue; }
                int index = 0;
                while (index < int(split->children.size()) && split->children[index]->id != child) ++index;
                if (index == int(split->children.size())) break;
                const int divider = edgeBefore(edge) ? index - 1 : index;
                if (split->axis == edgeAxis(edge) && divider >= 0 && divider < int(split->children.size()) - 1) {
                    const auto boundary = dragBoundary(m_doc.root, layoutMetrics(), split->id, divider);
                    if (boundary.valid) state.boundaries.push_back({split->id, divider, split->axis, boundary.drag.start});
                    break;
                }
                child = split->id;
            }
        }
    }
    m_cameraResizeTimer.stop();
    m_cameraResizePending = false;
    m_resize = std::move(state);
    m_flights.erase(w);
    m_tween.stop();
    m_camera.jump(m_resize->camera);
    changed();
    schedule();
}

void KrellisCanvas::resizeStepped(EffectWindow *w, const RectF &geometry)
{
    if (m_resize && m_resize->window == w) { m_resize->lastNative = geometry; ++m_resize->nativeSteps; }
    if (w->window()) resizeAt(w, w->window()->interactiveMoveResizeAnchor());
}

void KrellisCanvas::resizeAt(EffectWindow *w, QPointF pointer)
{
    if (!m_resize || m_resize->window != w) return;
    auto &state = *m_resize;
    const auto *tracked = trackedFor(w);
    if (!tracked) return;
    const QPointF delta = pointer - state.anchor;
    const auto gravity = w->window()->interactiveMoveResizeGravity();
    const bool leftEdge = gravity == Gravity::Left || gravity == Gravity::TopLeft || gravity == Gravity::BottomLeft;
    const bool rightEdge = gravity == Gravity::Right || gravity == Gravity::TopRight || gravity == Gravity::BottomRight;
    const bool topEdge = gravity == Gravity::Top || gravity == Gravity::TopLeft || gravity == Gravity::TopRight;
    const bool bottomEdge = gravity == Gravity::Bottom || gravity == Gravity::BottomLeft || gravity == Gravity::BottomRight;
    if (state.floating) {
        // Native edge deltas are logical pointer distances. Applying the paint
        // scale again would make an already scaled window lag behind the pointer.
        const auto &p = state.painted;
        const double left = p.x + (leftEdge ? delta.x() : 0);
        const double top = p.y + (topEdge ? delta.y() : 0);
        const double right = p.x + p.w + (rightEdge ? delta.x() : 0);
        const double bottom = p.y + p.h + (bottomEdge ? delta.y() : 0);
        const TRect normalized{(left - m_area.x()) / m_area.width(), (top - m_area.y()) / m_area.height(),
                               std::max(1.0, right - left) / m_area.width(), std::max(1.0, bottom - top) / m_area.height()};
        for (auto &floating : m_doc.floating)
            if (floating.panel->id == tracked->panel) floating.rect = clampFloat(normalized, krellis::FloatingLayer::Overlay, m_doc.floatMargin);
    } else {
        m_doc.root = state.before.root;
        for (const auto &boundary : state.boundaries) {
            const bool x = boundary.axis == Axis::X;
            const double distance = x ? delta.x() : delta.y();
            const double worldDelta = distance * (x ? state.camera.w / m_area.width() : state.camera.h / m_area.height());
            const auto drag = dragBoundary(m_doc.root, layoutMetrics(), boundary.split, boundary.index);
            if (drag.valid) m_doc.root = drag.drag.to(boundary.start + worldDelta);
        }
        // An edge tile squeezed to a sliver from its inner side collapses into a drawer on release.
        state.stash.reset();
        if (int(leftEdge) + int(rightEdge) + int(topEdge) + int(bottomEdge) == 1) {
            const auto *self = m_rects.find(tracked->panel);
            const auto *root = m_doc.root ? m_rects.find(m_doc.root->id) : nullptr;
            if (self && root) {
                const auto &a = self->rect, &b = root->rect;
                const double eps = 0.5;
                std::optional<krellis::Edge> want;
                if (leftEdge && std::abs(a.x + a.w - (b.x + b.w)) < eps && state.painted.w - delta.x() < m_drawerCollapsePx) want = krellis::Edge::Right;
                else if (rightEdge && std::abs(a.x - b.x) < eps && state.painted.w + delta.x() < m_drawerCollapsePx) want = krellis::Edge::Left;
                else if (topEdge && std::abs(a.y + a.h - (b.y + b.h)) < eps && state.painted.h - delta.y() < m_drawerCollapsePx) want = krellis::Edge::Bottom;
                else if (bottomEdge && std::abs(a.y - b.y) < eps && state.painted.h + delta.y() < m_drawerCollapsePx) want = krellis::Edge::Top;
                // Not where another display joins this one: the pointer crossing over would open the drawer.
                if (want && drawerEdgeFree(*want)) state.stash = want;
            }
        }
    }
    relayout(false);
    m_camera.jump(state.camera);
    updateMarquee(0);
}

void KrellisCanvas::finishResize(bool commit)
{
    if (!m_resize) return;
    auto state = std::move(*m_resize);
    m_resize.reset();
    // Structural changes can abort a preview while KWin still owns its grab.
    if (!commit && state.window && !state.window->isDeleted() && state.window->window()
        && state.window->window()->isInteractiveResize())
        state.window->window()->cancelInteractiveMoveResize();
    bool stashed = false;
    if (!commit) m_doc = std::move(state.before);
    else if (state.stash && state.window && trackedFor(state.window)) {
        stashed = true; // stashPanel records its own step
        const std::string panel = trackedFor(state.window)->panel;
        m_doc = state.before;
        stashPanel(panel, *state.stash, state.painted);
    } else if (state.floating && state.window) {
        if (const auto *tracked = trackedFor(state.window))
            for (auto &floating : m_doc.floating) {
                if (floating.panel->id != tracked->panel) continue;
                // A resized drawer stays hugging the edge it slides out of.
                if (const auto d = m_drawers.find(tracked->panel); d != m_drawers.end()) {
                    switch (d->second.edge) {
                    case krellis::Edge::Left: floating.rect.x = 0; break;
                    case krellis::Edge::Right: floating.rect.x = 1 - floating.rect.w; break;
                    case krellis::Edge::Top: floating.rect.y = 0; break;
                    case krellis::Edge::Bottom: floating.rect.y = 1 - floating.rect.h; break;
                    }
                } else {
                    m_rememberedFloats[tracked->panel] = floating.rect;
                }
            }
    }
    if (commit && !stashed) commitEdit("resize window", state.before, m_doc);
    relayout(true);
}

bool KrellisEffect::simNativeResizeStart(int index, double horizontal, double vertical)
{
    auto *canvas = current();
    if (!canvas || index < 0 || index >= int(canvas->m_windows.size())) return false;
    auto *effectWindow = canvas->m_windows[size_t(index)].window;
    auto *window = effectWindow->window();
    if (!window || (window->isInteractiveMove() || window->isInteractiveResize())) return false;
    markSessionInteraction();
    const auto rect = window->frameGeometry();
    effects->activateWindow(effectWindow);
    window->performMousePressCommand(Options::MouseUnrestrictedResize,
        QPointF(rect.x() + rect.width() * std::clamp(horizontal, 0.0, 1.0),
                rect.y() + rect.height() * std::clamp(vertical, 0.0, 1.0)));
    return window->isInteractiveResize() && canvas->m_resize.has_value();
}

bool KrellisEffect::simNativeResizeMove(double dx, double dy)
{
    auto *canvas = current();
    if (!canvas || !canvas->m_resize || !canvas->m_resize->window) return false;
    auto *window = canvas->m_resize->window->window();
    if (!window || !window->isInteractiveResize()) return false;
    window->updateInteractiveMoveResize(canvas->m_resize->anchor + QPointF(dx, dy), Qt::NoModifier);
    canvas->resizeAt(canvas->m_resize->window, canvas->m_resize->anchor + QPointF(dx, dy));
    return true;
}

bool KrellisEffect::simNativeResizeEnd(bool commit)
{
    auto *canvas = current();
    if (!canvas || !canvas->m_resize || !canvas->m_resize->window) return false;
    auto *window = canvas->m_resize->window->window();
    if (!window) return false;
    canvas->m_resize->decision = commit;
    if (commit) window->endInteractiveMoveResize(); else window->cancelInteractiveMoveResize();
    return !canvas->m_resize.has_value();
}
} // namespace KWin
