#include "krellis_effect.h"
#include "effect/effecthandler.h"
#include "window.h"
#include "pointer_input.h"
#include "wayland_server.h"
#include "wayland/seat.h"
#include "input_geometry.h"
#include <kdecoration3/decoration.h>
#include <QCoreApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMatrix4x4>

namespace KWin {

std::optional<KrellisEffect::SurfaceHit> KrellisEffect::surfaceHit(QPointF point) const
{
    auto hitWindow = [this, point](EffectWindow *w, bool grabbed, bool borders = false) -> std::optional<SurfaceHit> {
        auto *canvas = owner(w);
        if (!canvas || !w->window() || w->isMinimized() || w->isDeleted() || w->isHidden()
            || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) return std::nullopt;
        if (!grabbed && canvasAt(point) != canvas) return std::nullopt;
        const auto shown = canvas->visualRect(w);
        const auto f = w->frameGeometry();
        // Decorations reach past the frame with resize-only borders, which here lie in the tile gap.
        // A client-side-decorated window (Brave, GTK) keeps its resize band in the shadow its surface
        // draws outside the frame; the surface's input region decides in hitTest below.
        QRectF reach = shown ? QRectF(shown->x, shown->y, shown->w, shown->h) : QRectF();
        if (borders && shown && f.width() > 0 && f.height() > 0) {
            QMarginsF m;
            if (w->window()->decoration()) {
                m = w->window()->decoration()->resizeOnlyBorders();
            } else {
                const auto b = w->bufferGeometry();
                m = QMarginsF(std::max(0.0, f.left() - b.left()), std::max(0.0, f.top() - b.top()),
                              std::max(0.0, b.right() - f.right()), std::max(0.0, b.bottom() - f.bottom()));
            }
            const double sx = shown->w / f.width(), sy = shown->h / f.height();
            reach = reach.marginsAdded(QMarginsF(m.left() * sx, m.top() * sy, m.right() * sx, m.bottom() * sy));
        }
        if (!shown || (!grabbed && !reach.contains(point))) return std::nullopt;
        const auto mapped = krellis::mapPoint({point.x(), point.y()}, *shown, {f.x(), f.y(), f.width(), f.height()});
        if (!mapped) return std::nullopt;
        const QPointF rest(mapped->x, mapped->y);
        if (!grabbed && !w->window()->hitTest(rest)) return std::nullopt;
        const bool decoration = grabbed ? m_grabDecoration : w->window()->decoration() && !w->window()->clientGeometry().contains(rest);
        const bool nativeDecorationGrab = grabbed && m_grabDecoration
            && (w->isUserMove() || w->isUserResize());
        const bool busy = !nativeDecorationGrab && (canvas->m_nav.gestureActive()
            || canvas->m_drag != KrellisCanvas::Drag::None || canvas->m_session);
        const double scale = std::min(shown->w / f.width(), shown->h / f.height());
        const bool unusable = !decoration && !canvas->rulesFor(w).interactive(scale);
        const auto *tracked = canvas->trackedFor(w);
        const bool icon = tracked && canvas->frameOnly(tracked->panel);
        const bool outOfFocus = tracked && canvas->peeking(tracked->panel);
        return SurfaceHit{w, canvas, *shown, rest, decoration, busy || unusable || icon || outOfFocus};
    };
    if (m_clientGrab) return hitWindow(m_clientGrab, true);
    const auto stacking = effects->stackingOrder();
    for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
        auto *w = *it;
        if (w->isDeleted() || w->isMinimized() || w->isHidden() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) continue;
        // A fullscreen window is not transformed: KWin routes input to it, and nothing beneath may take it.
        if (w->isFullScreen() && owner(w) && w->frameGeometry().contains(point)) return std::nullopt;
        if (auto hit = hitWindow(w, false)) return hit;
        // A float's resize band belongs to it even where another window lies underneath; a docked tile's band
        // is only offered below, when no body is under the pointer (it would overlap its neighbours).
        if (auto *canvas = owner(w))
            if (auto *tracked = canvas->trackedFor(w); tracked && canvas->floatScreen(tracked->panel))
                if (auto hit = hitWindow(w, false, true)) return hit;
        // Native shell UI above a canvas keeps KWin's normal routing.
        if (!owner(w) && w->frameGeometry().contains(point) && !w->isDesktop()) return std::nullopt;
    }
    // Only when no window body is under the pointer: resize handles just outside a frame.
    for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
        auto *w = *it;
        if (w->isDeleted() || w->isMinimized() || w->isHidden() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) continue;
        if (auto hit = hitWindow(w, false, true)) return hit;
    }
    return std::nullopt;
}

void KrellisEffect::leaveSurface()
{
    leaveDecoration();
    if (!waylandServer() || !waylandServer()->seat()) {
        m_clientButtons.clear();
        m_clientGrab = nullptr;
        return;
    }
    auto *seat = waylandServer()->seat();
    for (const auto button : m_clientButtons)
        seat->notifyPointerButton(button, PointerButtonState::Released);
    if (!m_clientButtons.empty()) seat->notifyPointerFrame();
    m_clientButtons.clear();
    m_clientGrab = nullptr;
    seat->notifyPointerLeave();
}

void KrellisEffect::leaveDecoration()
{
    if (m_decorWindow && m_decorWindow->window() && m_decorWindow->window()->decoration()) {
        QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(), m_decorPosition);
        QCoreApplication::sendEvent(m_decorWindow->window()->decoration(), &leave);
    }
    if (m_decorWindow && m_decorWindow->window()
        && input()->pointer()->decoration() == m_decorWindow->window()->decoratedWindow())
        input()->pointer()->setDecoration(nullptr);
    m_decorWindow = nullptr;
    m_decorPosition = {};
}

void KrellisEffect::focusDecoration(const SurfaceHit &hit, QPointF position)
{
    auto *window = hit.window->window();
    const QPointF local = hit.mapped - QPointF(window->pos());
    if (input()->pointer()->decoration() != window->decoratedWindow())
        input()->pointer()->setDecoration(window->decoratedWindow());
    if (m_decorWindow != hit.window) {
        leaveDecoration();
        m_decorWindow = hit.window;
        QHoverEvent enter(QEvent::HoverEnter, local, position, QPointF(-1, -1));
        QCoreApplication::sendEvent(window->decoration(), &enter);
    }
    // Qt's four-position overload takes LOCAL scenePos first and GLOBAL pos
    // second. Swapping these makes sectionUnderMouse report a resize corner.
    QHoverEvent hover(QEvent::HoverMove, local, position, m_decorPosition);
    m_decorPosition = local;
    QCoreApplication::sendEvent(window->decoration(), &hover);
    window->processDecorationMove(local, position);
}

void KrellisEffect::focusSurface(const SurfaceHit &hit, QPointF point)
{
    leaveDecoration();
    auto *window = hit.window->window();
    const auto frame = hit.window->frameGeometry();
    QMatrix4x4 inverse;
    inverse.translate(frame.x(), frame.y());
    inverse.scale(frame.width() / hit.shown.w, frame.height() / hit.shown.h);
    inverse.translate(-hit.shown.x, -hit.shown.y);
    const QMatrix4x4 transformation = window->inputTransformation() * inverse;
    auto *seat = waylandServer()->seat();
    if (seat->focusedPointerSurface() != window->surface())
        seat->notifyPointerEnter(window->surface(), point, transformation);
    else
        seat->setFocusedPointerSurfaceTransformation(transformation);
    seat->notifyPointerMotion(point);
}

bool KrellisEffect::surfaceMotion(const PointerMotionEvent &event)
{
    if (m_pinchCanvas) { leaveSurface(); return true; }
    const auto hit = surfaceHit(event.position);
    if (!hit) {
        // An off-canvas neighbour may occupy real geometry under this output.
        auto *native = input()->findToplevel(event.position);
        if (native && native->effectWindow() && owner(native->effectWindow())) {
            leaveSurface();
            return true;
        }
        return false;
    }
    if (hit->inert || (!m_clientGrab && hit->canvas->overUi(event.position))) { leaveSurface(); return true; }
    auto *window = hit->window->window();
    auto *seat = waylandServer()->seat();
    seat->setTimestamp(event.timestamp);
    if (hit->decoration) {
        seat->notifyPointerLeave();
        focusDecoration(*hit, event.position);
    } else if (window->surface()) {
        focusSurface(*hit, event.position);
        const auto f = hit->window->frameGeometry();
        const QPointF scale(f.width() / hit->shown.w, f.height() / hit->shown.h);
        seat->relativePointerMotion({event.delta.x() * scale.x(), event.delta.y() * scale.y()},
            {event.deltaUnaccelerated.x() * scale.x(), event.deltaUnaccelerated.y() * scale.y()}, event.timestamp);
        seat->notifyPointerFrame();
    }
    return true;
}

bool KrellisEffect::surfaceButton(const PointerButtonEvent &event)
{
    if (m_pinchCanvas) { leaveSurface(); return true; }
    const auto hit = surfaceHit(event.position);
    if (event.state == PointerButtonState::Released && m_dismissedButtons.erase(event.nativeButton)) return true;
    if (event.state == PointerButtonState::Pressed) {
        const auto stack = effects->stackingOrder();
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            auto *popup = *it;
            if (!popup->isPopupWindow() || popup->isDeleted() || popup->isHidden() || !owner(popup)
                || !popup->window() || !popup->window()->hasPopupGrab()) continue;
            bool inside = false;
            for (auto *w = hit ? hit->window : nullptr; w; w = w->transientFor())
                if (w == popup) { inside = true; break; }
            if (!inside) {
                popup->window()->popupDone();
                m_dismissedButtons.insert(event.nativeButton);
                leaveSurface();
                return true;
            }
            break;
        }
    }
    if (!hit) {
        PointerMotionEvent motion{};
        motion.position = event.position;
        return surfaceMotion(motion);
    }
    if (hit->inert || (!m_clientGrab && hit->canvas->overUi(event.position))) { leaveSurface(); return true; }
    auto *window = hit->window->window();
    auto *seat = waylandServer()->seat();
    seat->setTimestamp(event.timestamp);
    if (event.state == PointerButtonState::Pressed) {
        m_clientGrab = hit->window;
        m_grabDecoration = hit->decoration;
        m_clientButtons.insert(event.nativeButton);
        if (!hit->window->isPopupWindow()) effects->activateWindow(hit->window);
    }
    if (hit->decoration) {
        seat->notifyPointerLeave();
        // A press can arrive without an intervening motion, or after native
        // focus tracking updated another decoration. Refresh the mapped section.
        if (event.state == PointerButtonState::Pressed) focusDecoration(*hit, event.position);
        const QPointF local = hit->mapped - QPointF(window->pos());
        QMouseEvent mouse(event.state == PointerButtonState::Pressed ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
            local, event.position, event.button, event.buttons, event.modifiers);
        mouse.setAccepted(false);
        QCoreApplication::sendEvent(window->decoration(), &mouse);
        if (event.state == PointerButtonState::Pressed && !mouse.isAccepted())
            window->processDecorationButtonPress(local, event.position, event.button);
        if (event.state == PointerButtonState::Released) window->processDecorationButtonRelease(event.button);
    } else if (window->surface()) {
        focusSurface(*hit, event.position);
        seat->notifyPointerButton(event.nativeButton, event.state);
        seat->notifyPointerFrame();
    }
    if (event.state == PointerButtonState::Released) {
        m_clientButtons.erase(event.nativeButton);
        if (m_clientButtons.empty()) m_clientGrab = nullptr;
    }
    return true;
}

bool KrellisEffect::surfaceAxis(const PointerAxisEvent &event)
{
    if (m_pinchCanvas) { leaveSurface(); return true; }
    const auto hit = surfaceHit(event.position);
    if (!hit) return false;
    if (hit->inert || (!m_clientGrab && hit->canvas->overUi(event.position))) { leaveSurface(); return true; }
    auto *window = hit->window->window();
    auto *seat = waylandServer()->seat();
    seat->setTimestamp(event.timestamp);
    if (hit->decoration) {
        const QPoint angle = event.orientation == Qt::Vertical ? QPoint(0, event.deltaV120) : QPoint(event.deltaV120, 0);
        QWheelEvent wheel(hit->mapped - QPointF(window->pos()), event.position, {}, angle,
            event.buttons, event.modifiers, Qt::NoScrollPhase, event.inverted);
        QCoreApplication::sendEvent(window->decoration(), &wheel);
    } else if (window->surface()) {
        focusSurface(*hit, event.position);
        seat->notifyPointerAxis(event.orientation, event.delta, event.deltaV120, event.source, event.inverted);
        seat->notifyPointerFrame();
    }
    return true;
}

} // namespace KWin
