// Pinned windows: a pinned tile keeps its slot in the layout, and while that slot is mostly out of view the window is
// drawn live as a thumbnail in the bottom-left corner, above everything else. Clicking the thumbnail reveals the slot.
// The thumbnail is just another place visualRect can put a window, so painting, the glide to and from it and the
// pointer all follow without special cases.
#include "krellis_effect.h"
#include "effect/effecthandler.h"
#include <algorithm>

namespace KWin {
using namespace krellis;

namespace {
constexpr double PinMargin = 16, PinGap = 12;

QSizeF thumbSize(const QRectF &area, const QRectF &frame)
{
    if (frame.width() <= 0 || frame.height() <= 0) return {};
    const double wide = std::clamp(area.width() * 0.22, 220.0, 440.0);
    const double scale = std::min({1.0, wide / frame.width(), area.height() * 0.3 / frame.height()});
    return {frame.width() * scale, frame.height() * scale};
}
}

bool KrellisCanvas::isPinned(const std::string &panel) const
{
    return std::any_of(m_pins.begin(), m_pins.end(), [&](const Pin &p) { return p.panel == panel; });
}

// How much of the slot is in view once the camera arrives, as a share of the most of it that could be: a tile larger
// than the screen is judged against the screen, so zooming into one never throws it into the corner.
double KrellisCanvas::pinVisibility(const std::string &panel) const
{
    const TRect *world = worldRect(panel);
    if (!world) return 1;
    const TRect r = panelAt(*world, m_camera.target);
    const double area = r.w * r.h;
    if (area <= 0) return 1;
    const QRectF in = QRectF(r.x, r.y, r.w, r.h).intersected(m_screen);
    if (in.isEmpty()) return 0;
    return in.width() * in.height() / std::min(area, m_screen.width() * m_screen.height());
}

// Where a shown pin's thumbnail sits: stacked upward from the bottom-left corner above the bar, in the order the
// windows were pinned, and into a second column once a stack reaches the top.
std::optional<TRect> KrellisCanvas::pinRect(const std::string &panel) const
{
    const double bottom = m_screen.bottom() - barHeight() - PinMargin;
    double x = m_screen.left() + PinMargin, y = bottom, column = 0;
    for (const Pin &pin : m_pins) {
        if (!pin.shown || !pin.window) continue;
        const QSizeF size = thumbSize(m_screen, pin.window->frameGeometry());
        if (size.isEmpty()) continue;
        if (y != bottom && y - size.height() < m_screen.top() + PinMargin) {
            x += column + PinGap;
            y = bottom;
            column = 0;
        }
        y -= size.height();
        if (pin.panel == panel) return TRect{x, y, size.width(), size.height()};
        y -= PinGap;
        column = std::max(column, size.width());
    }
    return std::nullopt;
}

void KrellisCanvas::updatePins()
{
    if (m_pins.empty()) return;
    // The windows a pin outlived: closed, or sent to another output.
    for (const Pin &pin : m_pins)
        if (pin.window && pin.shown && !trackedFor(pin.window)) effects->setElevatedWindow(pin.window, false);
    std::erase_if(m_pins, [this](const Pin &pin) { return !pin.window || !trackedFor(pin.window) || trackedFor(pin.window)->panel != pin.panel; });

    struct Change { Pin *pin; TRect from; double opacity; };
    std::vector<Change> changes;
    for (Pin &pin : m_pins) {
        EffectWindow *w = pin.window;
        const bool usable = !m_overlaysCovered && !m_resize && !w->isMinimized() && !w->isHidden() && !w->isFullScreen()
            && !floatScreen(pin.panel) && !m_drawers.count(pin.panel) && worldRect(pin.panel)
            && !(m_session && m_session->active() && w == m_dragWindow);
        const double seen = usable ? pinVisibility(pin.panel) : 1;
        const bool want = usable && (pin.shown ? seen < PIN_HIDE_ABOVE : seen < PIN_SHOW_BELOW);
        if (want == pin.shown) continue;
        double opacity = 1;
        changes.push_back({&pin, visualRect(w, &opacity).value_or(screenRect(pin.panel)), opacity});
    }
    for (const Change &c : changes) {
        c.pin->shown = !c.pin->shown;
        effects->setElevatedWindow(c.pin->window, c.pin->shown);
        if (!c.pin->window->isMinimized() && !c.pin->window->isHidden())
            startFlight(c.pin->window, FlightKind::Glide, c.from, {}, c.opacity);
    }
    // A thumbnail whose neighbour came or went has a new place in the stack: it glides there from where it was drawn.
    for (Pin &pin : m_pins) {
        if (!pin.shown || m_flights.count(pin.window)) continue;
        const Tracked *t = trackedFor(pin.window);
        const auto now = pinRect(pin.panel);
        if (t && t->lastDrawn && now && (std::abs(now->x - t->lastDrawn->x) > 0.5 || std::abs(now->y - t->lastDrawn->y) > 0.5))
            startFlight(pin.window, FlightKind::Glide, *t->lastDrawn, {});
    }
    if (!changes.empty()) schedule();
}

EffectWindow *KrellisCanvas::pinAt(QPointF p) const
{
    for (auto it = m_pins.rbegin(); it != m_pins.rend(); ++it) {
        if (!it->shown || !it->window) continue;
        if (const auto shown = visualRect(it->window); shown && QRectF(shown->x, shown->y, shown->w, shown->h).contains(p))
            return it->window;
    }
    return nullptr;
}

void KrellisCanvas::setPinOn(bool on)
{
    m_pinOn = on;
    if (on || m_pins.empty()) return;
    struct Back { QPointer<EffectWindow> window; TRect from; double opacity; };
    std::vector<Back> back;
    for (const Pin &pin : m_pins) {
        if (!pin.window || !pin.shown) continue;
        double opacity = 1;
        const TRect from = visualRect(pin.window, &opacity).value_or(screenRect(pin.panel));
        effects->setElevatedWindow(pin.window, false);
        back.push_back({pin.window, from, opacity});
    }
    m_pins.clear();
    for (const auto &u : back)
        if (u.window && !u.window->isMinimized() && !u.window->isHidden()) startFlight(u.window, FlightKind::Glide, u.from, {}, u.opacity);
    schedule();
}

void KrellisCanvas::togglePinActive()
{
    const int i = activeIndex();
    if (i < 0) return;
    Tracked &t = m_windows[size_t(i)];
    EffectWindow *w = t.window;
    const std::string panel = t.panel;
    if (floatScreen(panel)) {
        toast(QStringLiteral("A floating window already stays on screen"));
        return;
    }
    const auto it = std::find_if(m_pins.begin(), m_pins.end(), [&](const Pin &p) { return p.panel == panel; });
    if (it != m_pins.end()) {
        if (it->shown) {
            double opacity = 1;
            const TRect from = visualRect(w, &opacity).value_or(screenRect(panel));
            m_pins.erase(it);
            effects->setElevatedWindow(w, false);
            startFlight(w, FlightKind::Glide, from, {}, opacity);
        } else {
            m_pins.erase(it);
        }
        toast(QStringLiteral("Unpinned"));
    } else {
        m_pins.push_back({panel, w, false});
        toast(QStringLiteral("Pinned: shows in the corner while its tile is out of view"));
    }
    updatePins();
    schedule();
}

} // namespace KWin
