#include "krellis_effect.h"
#include "boot_guard.h"
#include "core/renderviewport.h"

#include <chrono>
#include "input_geometry.h"

#include <QFile>
#include <KGlobalAccel>
#include <QAction>
#include <QKeySequence>
#include <QKeyEvent>

#include "effect/effecthandler.h"
#include "effect/timeline.h"
#include "effect/offscreenquickview.h"
#include <QQuickItem>
#include "window.h"
#include "pointer_input.h"
#include "core/output.h"

#include <QDBusConnection>
#include <algorithm>
#include <QLoggingCategory>
#include <QStringList>
#include <QTimer>
#include <QScopedValueRollback>
#include <cmath>

Q_LOGGING_CATEGORY(krellisLog, "krellis")

namespace KWin {

using namespace krellis;

namespace {
// --krellis-ease and the appear animation from the Krellis stylesheet.
const std::function<double(double)> APPEAR_EASE = krellis::cubicBezier(0.22, 1, 0.36, 1);
constexpr double APPEAR_MS = 220;
constexpr double CLOSE_MS = 180;
constexpr double BAR_H = 48;
constexpr double SteppingPatienceMs = 120; // longest a gliding client may sit on a size request before being asked again
constexpr double MaxHeldSide = 8192;       // longest side asked of a client held above its tile size
constexpr double LandingPatienceMs = 300; // longest a stale buffer is drawn at its own size while its client lays out
}

static constexpr double GAP = 6; // px between tiles

KrellisCanvas::KrellisCanvas(LogicalOutput *output)
    : m_output(output)
    , m_nav(*this, m_camera)
{
    loadRules();
    connect(effects, &EffectsHandler::windowClosed, this, &KrellisCanvas::windowClosed);
    connect(effects, &EffectsHandler::windowActivated, this, &KrellisCanvas::windowActivated);
    connect(effects, &EffectsHandler::windowAdded, this, [this](EffectWindow *w) { watch(w); });
    for (EffectWindow *w : effects->stackingOrder())
        watch(w);
    // Struts can change without output geometry changing (panel visibility,
    // thickness or edge). Query KWin after it has recomputed its client areas.
    auto *areaTimer = new QTimer(this);
    areaTimer->setInterval(100);
    connect(areaTimer, &QTimer::timeout, this, [this] {
        const auto outputs = effects->screens();
        if (!outputs.contains(m_output))
            return;
        const auto r = effects->clientArea(MaximizeArea, m_output);
        const QRectF usable(r.x(), r.y(), r.width(), r.height());
        if (usable == m_screen)
            return;
        if (m_resize) finishResize(false);
        std::map<EffectWindow *, std::pair<TRect, double>> before;
        for (const auto &t : m_windows)
            if (!t.window->isMinimized()) {
                TRect rect = screenRect(t.panel);
                double opacity = 1;
                flightRect(t.window, t, rect, opacity);
                before[t.window] = {rect, opacity};
            }
        relayout(true);
        for (const auto &[window, paint] : before)
            startFlight(window, FlightKind::Glide, paint.first, {}, paint.second);
    });
    areaTimer->start();
    relayout(false);
}

KrellisCanvas::~KrellisCanvas() = default;

void KrellisCanvas::prepareUnload()
{
    m_unloading = true;
    for (const auto &t : m_windows) effects->setElevatedWindow(t.window, false);
    for (const auto &t : m_stowed) effects->setElevatedWindow(t.window, false);
    m_dragTabView.clear();
    m_tabPress.reset();
    m_tabStrips.reset();
    m_changed = {};
    m_transfer = {};
    for (auto *timer : findChildren<QTimer *>()) timer->stop();
    disconnect(effects, nullptr, this, nullptr);
    for (auto *window : effects->stackingOrder()) {
        disconnect(window, nullptr, this, nullptr);
        if (window->window()) disconnect(window->window(), nullptr, this, nullptr);
    }
    if (m_session) {
        const auto cancelled = m_session->end(false, m_nowMs);
        if (cancelled.wasDrag) m_doc = cancelled.next;
        m_session.reset();
    }
    if (m_resize) {
        m_doc = m_resize->before;
        if (m_resize->window && m_resize->window->window())
            m_resize->window->window()->cancelInteractiveMoveResize();
    }
    m_resize.reset();
    m_dragWindow = nullptr;
    m_dragFromKWin = false;
    m_drag = Drag::None;
    m_naming = false;
    m_iconPressed.clear();
    m_nav.endGesture();
    m_tween.stop();
    m_flights.clear();
    m_landings.clear();
    m_popups.clear();
    m_closing.clear();
    m_slot.reset();
    m_marquee.reset();
    m_drawerTabs.reset();
    m_toast.reset();
    m_toastText.clear();
    m_toastBusy = m_toastHoldArmed = m_toastHoldDone = false;
    ++m_toastGen;
    m_sheet.reset();
    m_sheetWanted = false;
    m_sheetOpacity = 0;
    m_sheetBusy = false;
    m_palette.reset();
    m_paletteOn = m_paletteBusy = m_paletteDirty = false;
    m_paletteOpacity = 0;
    m_paletteItems.clear();
    m_paletteRanked.clear();
    m_animating = false;
}

int KrellisCanvas::activeIndex() const
{
    EffectWindow *active = effects->activeWindow();
    for (size_t i = 0; i < m_windows.size(); ++i)
        if (m_windows[i].window == active)
            return int(i);
    return -1;
}

void KrellisCanvas::cyclePanel(int step)
{
    // Visit windows in the order they were opened, skipping the ones parked in the dock.
    std::vector<size_t> shown;
    for (size_t i = 0; i < m_windows.size(); ++i)
        if (!m_windows[i].window->isMinimized() && !scratchWindow(m_windows[i].window))
            shown.push_back(i);
    if (shown.empty())
        return;
    const int cur = activeIndex();
    const auto at = std::find(shown.begin(), shown.end(), size_t(cur < 0 ? 0 : cur));
    long pos = at == shown.end() ? 0 : long(at - shown.begin());
    pos = ((pos + step) % long(shown.size()) + long(shown.size())) % long(shown.size());
    const size_t pick = shown[size_t(pos)];
    m_nav.reveal(m_windows[pick].panel);
    effects->activateWindow(m_windows[pick].window);
    applyRest();
}
// The active window's docked panel, or empty (nothing active here, or it floats).
static std::string dockedPanelOf(const KrellisCanvas &canvas, const krellis::LayoutDocument &doc, int index, const std::vector<std::string> &panels)
{
    Q_UNUSED(canvas)
    if (index < 0 || size_t(index) >= panels.size()) return {};
    const auto where = krellis::locatePanel(doc, panels[size_t(index)]);
    return where.where == krellis::PanelLocation::Where::Docked ? panels[size_t(index)] : std::string();
}

void KrellisCanvas::focusDirection(krellis::Edge dir)
{
    std::vector<std::string> panels;
    for (const auto &t : m_windows) panels.push_back(t.panel);
    const std::string from = dockedPanelOf(*this, m_doc, activeIndex(), panels);
    if (from.empty() || m_session) return;
    const std::string to = krellis::neighbourInDirection(m_rects, from, dir);
    if (to.empty()) return;
    const auto at = std::find_if(m_windows.begin(), m_windows.end(), [&](const Tracked &t) { return t.panel == to && !t.window->isMinimized(); });
    if (at == m_windows.end()) return;
    m_nav.reveal(to);
    effects->activateWindow(at->window);
    applyRest();
}

bool KrellisCanvas::moveDirection(krellis::Edge dir)
{
    if (m_resize) finishResize(false);
    std::vector<std::string> panels;
    for (const auto &t : m_windows) panels.push_back(t.panel);
    const std::string panel = dockedPanelOf(*this, m_doc, activeIndex(), panels);
    if (panel.empty() || m_session) return true;
    bool moved = false;
    auto next = krellis::moveInDirection(m_doc, panel, dir, layoutMetrics(), &moved);
    if (!moved) return false;
    EditScope edit(*this, "move tile");
    m_doc = std::move(next);
    relayout(true);
    m_nav.reveal(panel);
    applyRest();
    return true;
}

void KrellisCanvas::enterFrom(EffectWindow *w, krellis::Edge entry)
{
    const auto *tracked = trackedFor(w);
    if (!tracked || m_session) return;
    const std::string panel = tracked->panel;
    if (krellis::locatePanel(m_doc, panel).where != krellis::PanelLocation::Where::Docked) return;
    EditScope edit(*this, "move tile");
    m_doc = krellis::dockAtEdge(std::move(m_doc), panel, entry);
    relayout(true);
    m_nav.reveal(panel);
    applyRest();
}

bool KrellisCanvas::resizeStep(krellis::Edge dir, double fraction)
{
    if (m_resize) finishResize(false);
    std::vector<std::string> panels;
    for (const auto &t : m_windows) panels.push_back(t.panel);
    const std::string panel = dockedPanelOf(*this, m_doc, activeIndex(), panels);
    if (panel.empty() || m_session) return false;
    const auto root = krellis::resizeInDirection(m_doc.root, layoutMetrics(), panel, dir, fraction);
    if (!root) return false;
    EditScope edit(*this, "resize tile", "resize:" + panel);
    m_doc.root = root;
    relayout(false);
    return true;
}

void KrellisCanvas::nextPanel() { cyclePanel(1); }
void KrellisCanvas::previousPanel() { cyclePanel(-1); }
void KrellisCanvas::toggleMaximizeActive() { toggleMaximize(activeIndex()); }
void KrellisCanvas::toggleFloatActive() { toggleFloat(activeIndex()); }
void KrellisCanvas::toggleHideActive() { toggleHide(activeIndex()); }
void KrellisCanvas::closeActive()
{
    const int i = activeIndex();
    if (i >= 0 && rulesFor(m_windows[size_t(i)].window).closable)
        m_windows[size_t(i)].window->closeWindow();
}

void KrellisEffect::registerShortcuts()
{
    // Meta-based so they do not collide with Plasma's Ctrl+Alt desktop switching.
    struct Binding {
        const char *id, *text;
        QKeySequence keys;
        void (KrellisEffect::*slot)();
        QKeySequence extra = {}; // a second chord for the same action, empty for none
    };
    const Binding bindings[] = {
        {"KrellisOverview", "Krellis: Toggle Overview", Qt::META | Qt::ALT | Qt::Key_Up, &KrellisEffect::toggleOverview},
        {"KrellisBack", "Krellis: Back", Qt::META | Qt::ALT | Qt::Key_Left, &KrellisEffect::back},
        {"KrellisForward", "Krellis: Forward", Qt::META | Qt::ALT | Qt::Key_Right, &KrellisEffect::forward},
        {"KrellisStepOut", "Krellis: Step Out", Qt::META | Qt::ALT | Qt::Key_Down, &KrellisEffect::stepOut},
        {"KrellisNextPanel", "Krellis: Next Panel", Qt::META | Qt::Key_BracketRight, &KrellisEffect::nextPanel},
        {"KrellisPreviousPanel", "Krellis: Previous Panel", Qt::META | Qt::Key_BracketLeft, &KrellisEffect::previousPanel},
        {"KrellisMaximize", "Krellis: Maximize Panel", Qt::META | Qt::SHIFT | Qt::Key_Return, &KrellisEffect::toggleMaximizeActive},
        {"KrellisFloat", "Krellis: Float Panel", Qt::META | Qt::SHIFT | Qt::Key_F, &KrellisEffect::toggleFloatActive},
        {"KrellisStash", "Krellis: Stash Window in Edge Drawer", Qt::META | Qt::SHIFT | Qt::Key_X, &KrellisEffect::stashActive},
        {"KrellisFlip", "Krellis: Flip Stacked / Side by Side", Qt::META | Qt::SHIFT | Qt::Key_T, &KrellisEffect::flipActive},
        {"KrellisHide", "Krellis: Hide Panel to Dock", Qt::META | Qt::SHIFT | Qt::Key_H, &KrellisEffect::toggleHideActive},
        {"KrellisDock", "Krellis: Toggle Dock", Qt::META | Qt::SHIFT | Qt::Key_D, &KrellisEffect::toggleDockActive},
        {"KrellisClose", "Krellis: Close Panel", Qt::META | Qt::SHIFT | Qt::Key_W, &KrellisEffect::closeActive},
        {"KrellisOff", "Krellis: Turn Off", Qt::META | Qt::ALT | Qt::Key_Escape, &KrellisEffect::turnOff},
        {"KrellisMode", "Krellis: Toggle Floating-First Mode", Qt::META | Qt::ALT | Qt::Key_F, &KrellisEffect::toggleFloatFirst},
        {"KrellisBar", "Krellis: Toggle Bar", Qt::META | Qt::ALT | Qt::Key_B, &KrellisEffect::toggleBar},
        {"KrellisMap", "Krellis: Toggle Map", Qt::META | Qt::ALT | Qt::Key_M, &KrellisEffect::toggleMap},
        {"KrellisFocusLeft", "Krellis: Focus Left", Qt::META | Qt::CTRL | Qt::Key_Left, &KrellisEffect::focusLeft},
        {"KrellisFocusRight", "Krellis: Focus Right", Qt::META | Qt::CTRL | Qt::Key_Right, &KrellisEffect::focusRight},
        {"KrellisFocusUp", "Krellis: Focus Up", Qt::META | Qt::CTRL | Qt::Key_Up, &KrellisEffect::focusUp},
        {"KrellisFocusDown", "Krellis: Focus Down", Qt::META | Qt::CTRL | Qt::Key_Down, &KrellisEffect::focusDown},
        {"KrellisMoveLeft", "Krellis: Move Panel Left", Qt::META | Qt::Key_Left, &KrellisEffect::moveLeft, QKeySequence(Qt::META | Qt::Key_A)},
        {"KrellisMoveRight", "Krellis: Move Panel Right", Qt::META | Qt::Key_Right, &KrellisEffect::moveRight, QKeySequence(Qt::META | Qt::Key_F)},
        {"KrellisMoveUp", "Krellis: Move Panel Up", Qt::META | Qt::Key_Up, &KrellisEffect::moveUp, QKeySequence(Qt::META | Qt::Key_D)},
        {"KrellisMoveDown", "Krellis: Move Panel Down", Qt::META | Qt::Key_Down, &KrellisEffect::moveDown, QKeySequence(Qt::META | Qt::Key_S)},
        {"KrellisResizeMode", "Krellis: Resize Mode", Qt::META | Qt::CTRL | Qt::Key_R, &KrellisEffect::toggleResizeMode},
        {"KrellisSaveView", "Krellis: Save View", Qt::META | Qt::ALT | Qt::Key_S, &KrellisEffect::saveView},
        {"KrellisPalette", "Krellis: Quick Jump", Qt::META | Qt::Key_Slash, &KrellisEffect::togglePalette},
        {"KrellisUndo", "Krellis: Undo Layout Edit", Qt::META | Qt::ALT | Qt::Key_Z, &KrellisEffect::undo},
        {"KrellisRedo", "Krellis: Redo Layout Edit", Qt::META | Qt::ALT | Qt::SHIFT | Qt::Key_Z, &KrellisEffect::redo},
    };
    for (const Binding &b : bindings) {
        auto *a = new QAction(this);
        a->setObjectName(QString::fromLatin1(b.id));
        a->setText(QString::fromLatin1(b.text));
        QList<QKeySequence> chords{b.keys};
        if (!b.extra.isEmpty()) chords << b.extra;
        KGlobalAccel::self()->setGlobalShortcut(a, chords);
        connect(a, &QAction::triggered, this, [this, slot = b.slot] { hideHint(); (this->*slot)(); });
    }
}

// Registered on demand so a disabled scratchpad does not hold Meta+G (KWin's Grid View wants it too).
void KrellisEffect::syncScratchShortcuts()
{
    if (!m_scratchpad) {
        qDeleteAll(m_scratchActions);
        m_scratchActions.clear();
        return;
    }
    if (!m_scratchActions.isEmpty()) return;
    struct Binding {
        const char *id, *text;
        QKeySequence keys;
        void (KrellisEffect::*slot)();
    };
    const Binding bindings[] = {
        {"KrellisScratchpad", "Krellis: Toggle Scratchpad", Qt::META | Qt::Key_G, &KrellisEffect::toggleScratchPad},
        {"KrellisScratchMark", "Krellis: Mark Window as Scratchpad", Qt::META | Qt::SHIFT | Qt::Key_G, &KrellisEffect::toggleScratchMarkSlot},
    };
    for (const Binding &b : bindings) {
        auto *a = new QAction(this);
        a->setObjectName(QString::fromLatin1(b.id));
        a->setText(QString::fromLatin1(b.text));
        KGlobalAccel::self()->setGlobalShortcut(a, b.keys);
        connect(a, &QAction::triggered, this, [this, slot = b.slot] { hideHint(); (this->*slot)(); });
        m_scratchActions << a;
    }
}

// Registered on demand: pin is opt-in, so a disabled one does not hold Meta+Shift+P.
void KrellisEffect::syncPinShortcut()
{
    if (!m_pinOn) {
        qDeleteAll(m_pinActions);
        m_pinActions.clear();
        return;
    }
    if (!m_pinActions.isEmpty()) return;
    auto *a = new QAction(this);
    a->setObjectName(QStringLiteral("KrellisPin"));
    a->setText(QStringLiteral("Krellis: Pin Panel"));
    KGlobalAccel::self()->setGlobalShortcut(a, QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_P));
    connect(a, &QAction::triggered, this, [this] { hideHint(); pinActive(); });
    m_pinActions << a;
}

void KrellisCanvas::watch(EffectWindow *w)
{
    if (w->window()) {
        connect(w->window(), &Window::interactiveMoveResizeStepped, this, [this, w](const RectF &geometry) {
            resizeStepped(w, geometry);
        });
        connect(w->window(), &Window::iconChanged, this, [this, w] {
            if (const auto *t = trackedFor(w)) { m_tiles[t->panel].icon = QString(); schedule(); }
        });
        connect(w->window(), &Window::captionChanged, this, [this, w] {
            if (trackedFor(w) || stowedFor(w)) { m_barSignature.clear(); updateBar(); updateTabStrips(); schedule(); }
        });
    }
    // A title-bar double click (or the maximize button) maximizes natively; Krellis answers with its own
    // panel maximize and gives the window its tile back.
    connect(w, &EffectWindow::windowMaximizedStateChanged, this, [this, w](EffectWindow *, bool horizontal, bool vertical) {
        if (!horizontal && !vertical) return;
        QPointer<EffectWindow> guard(w);
        QTimer::singleShot(0, this, [this, guard] {
            if (!guard || !guard->window() || m_unloading) return;
            const auto *t = trackedFor(guard);
            if (!t || floatScreen(t->panel)) return;
            const int index = int(t - m_windows.data());
            guard->window()->setMaximize(false, false);
            toggleMaximize(index);
        });
    });
    connect(w, &EffectWindow::minimizedChanged, this, &KrellisCanvas::minimizedChanged);
    // A video going fullscreen (Brave, mpv) must really cover the output, so it is drawn and placed by KWin
    // while it lasts; leaving it hands the window back to its tile.
    connect(w, &EffectWindow::windowFullScreenChanged, this, [this](EffectWindow *win) {
        if (m_unloading) return;
        m_landings.erase(win);
        if (auto *t = trackedFor(win); t && !win->isFullScreen()) t->requested.reset();
        applyRest();
        QPointer<EffectWindow> guard(win);
        QTimer::singleShot(0, this, [this, guard] { // KWin restores its remembered geometry after the signal
            if (guard && !m_unloading && !guard->isFullScreen()) applyRest();
        });
        schedule();
    });
    connect(w, &EffectWindow::windowStartUserMovedResized, this, &KrellisCanvas::userMoveStarted);
    connect(w, &EffectWindow::windowStepUserMovedResized, this, [this](EffectWindow *win) {
        if (!m_dragFromKWin || win != m_dragWindow)
            return;
        const QPointF pointer = win->window() ? win->window()->interactiveMoveResizeAnchor() : effects->cursorPos();
        if (m_handoff && m_output && !QRectF(QRect(m_output->geometry())).contains(pointer) && m_handoff(win, pointer))
            return;
        moveDrag(pointer);
    });
    connect(w, &EffectWindow::windowFinishUserMovedResized, this, &KrellisCanvas::userMoveFinished);
}

bool KrellisCanvas::manageable(EffectWindow *w)
{
    // A window that opens already fullscreen (Spectacle's region overlay, a game, a slideshow) is meant to cover the
    // screen, not to take a tile; windows that go fullscreen later are already tracked and unaffected.
    return w && !w->isFullScreen() && !(w->window() && w->window()->inherits("KWin::LayerShellV1Window")) && w->isNormalWindow() && !w->isDeleted() && !w->isSpecialWindow() && !w->isPopupWindow()
        && !w->isOnScreenDisplay() && !w->isNotification();
}

KrellisCanvas::Tracked *KrellisCanvas::trackedFor(EffectWindow *w)
{
    for (auto &t : m_windows)
        if (t.window == w)
            return &t;
    return nullptr;
}

KrellisCanvas::Tracked *KrellisCanvas::trackedForPanel(const std::string &panel)
{
    const auto loc = krellis::locatePanel(m_doc, panel);
    if (!loc.panel)
        return nullptr;
    for (auto &t : m_windows)
        if (t.panel == panel && (loc.panel->selected.empty() || t.view == loc.panel->selected))
            return &t;
    return nullptr;
}

Point KrellisCanvas::viewport() const
{
    return {m_area.width(), m_area.height()};
}

LayoutMetrics KrellisCanvas::layoutMetrics() const
{
    LayoutMetrics m;
    m.width = m_area.width();
    m.height = m_area.height();
    // Pixel minimums: a panel needs a usable client; splits sum along their axis, max across.
    m.min = [this](const Node &n, Axis axis) -> double {
        const auto minOf = [this](const auto &self, const Node &node, Axis a) -> double {
            if (node.kind == Node::Kind::Stage)
                return node.child ? self(self, *node.child, a) : 0.0;
            if (node.kind != Node::Kind::Split) {
                const auto minimum = minSize(node.views);
                return a == Axis::X ? minimum.x : minimum.y;
            }
            double total = 0;
            for (const auto &c : node.children) {
                const double v = self(self, *c, a);
                total = (a == node.axis) ? total + v : std::max(total, v);
            }
            return total;
        };
        return minOf(minOf, n, axis);
    };
    return m;
}

std::unique_ptr<OffscreenQuickScene> KrellisCanvas::makeScene(const QString &file)
{
    auto scene = std::make_unique<OffscreenQuickScene>();
    scene->setSource(QUrl(QStringLiteral("qrc:/" KRELLIS_ID "/") + file), QVariantMap{{QStringLiteral("theme"), QVariant::fromValue(static_cast<QObject *>(m_theme))}});
    if (!scene->rootItem()) {
        qCWarning(krellisLog) << file << "failed to load";
        return nullptr;
    }
    return scene;
}

void KrellisCanvas::themeChanged()
{
    schedule(); // the scenes' bindings follow the tokens by themselves; this only repaints
}

void KrellisCanvas::schedule()
{
    if (m_unloading) return;
    m_animating = true;
    effects->addRepaintFull();
}

TRect KrellisCanvas::panelAt(const TRect &world, const TRect &cam) const
{
    const double W = m_area.width(), H = m_area.height();
    return {m_area.x() + (world.x - cam.x) / cam.w * W + GAP, m_area.y() + (world.y - cam.y) / cam.h * H + GAP,
            world.w / cam.w * W - 2 * GAP, world.h / cam.h * H - 2 * GAP};
}

const TRect *KrellisCanvas::worldRect(const std::string &panel) const
{
    if (m_session && m_session->active()) {
        const auto &targets = m_session->layoutTargets();
        const auto it = targets.find(panel);
        return it == targets.end() ? nullptr : &it->second;
    }
    const Entry *e = m_rects.find(panel);
    return e ? &e->rect : nullptr;
}

// Overlay floats live in viewport-normalised coordinates, independent of the camera.
std::optional<TRect> KrellisCanvas::floatScreen(const std::string &panel) const
{
    for (const auto &f : m_doc.floating)
        if (f.panel->id == panel) {
            TRect r{m_area.x() + f.rect.x * m_area.width(), m_area.y() + f.rect.y * m_area.height(),
                    f.rect.w * m_area.width(), f.rect.h * m_area.height()};
            if (const auto d = m_drawers.find(panel); d != m_drawers.end() && !d->second.open) {
                switch (d->second.edge) {
                case krellis::Edge::Left: r.x = m_area.x() + m_drawerStripPx - r.w; break;
                case krellis::Edge::Right: r.x = m_area.right() - m_drawerStripPx; break;
                case krellis::Edge::Top: r.y = m_area.y() + m_drawerStripPx - r.h; break;
                case krellis::Edge::Bottom: r.y = m_area.bottom() - m_drawerStripPx; break;
                }
            }
            return r;
        }
    return std::nullopt;
}

// Closed drawers on one edge share it, in the order they were stashed (first at the top or left), so each sliver is its own target.
// The minimap shows a closed drawer as the sliver it leaves on screen (its own slice of it), not the size it opens to.
TRect KrellisCanvas::drawerMapRect(const std::string &panel, const krellis::Rect &open) const
{
    const auto d = m_drawers.find(panel);
    const auto slice = d != m_drawers.end() && !d->second.open ? drawerSlice(panel) : std::nullopt;
    if (!slice || m_area.width() <= 0 || m_area.height() <= 0) return {open.x, open.y, open.w, open.h};
    TRect r{(slice->x - m_area.x()) / m_area.width(), (slice->y - m_area.y()) / m_area.height(), slice->w / m_area.width(), slice->h / m_area.height()};
    // Along the edge only what the window itself covers shows.
    const bool vertical = d->second.edge == krellis::Edge::Left || d->second.edge == krellis::Edge::Right;
    double &from = vertical ? r.y : r.x, &size = vertical ? r.h : r.w;
    const double lo = std::max(from, vertical ? open.y : open.x), hi = std::min(from + size, vertical ? open.y + open.h : open.x + open.w);
    if (hi > lo) { from = lo; size = hi - lo; }
    // The real sliver is a few pixels; give the map one that can be seen.
    constexpr double minimum = 0.025;
    switch (d->second.edge) {
    case krellis::Edge::Left: r.x = 0; r.w = std::max(r.w, minimum); break;
    case krellis::Edge::Right: r.w = std::max(r.w, minimum); r.x = 1 - r.w; break;
    case krellis::Edge::Top: r.y = 0; r.h = std::max(r.h, minimum); break;
    case krellis::Edge::Bottom: r.h = std::max(r.h, minimum); r.y = 1 - r.h; break;
    }
    return r;
}

std::optional<TRect> KrellisCanvas::drawerSlice(const std::string &panel) const
{
    const auto self = m_drawers.find(panel);
    if (self == m_drawers.end()) return std::nullopt;
    const auto edge = self->second.edge;
    const bool vertical = edge == krellis::Edge::Left || edge == krellis::Edge::Right;
    std::vector<std::pair<int, std::string>> along;
    for (const auto &f : m_doc.floating) {
        const auto d = m_drawers.find(f.panel->id);
        if (d == m_drawers.end() || d->second.edge != edge) continue;
        along.emplace_back(d->second.seq, f.panel->id);
    }
    std::sort(along.begin(), along.end());
    const size_t n = along.size();
    const size_t i = size_t(std::find_if(along.begin(), along.end(), [&](const auto &a) { return a.second == panel; }) - along.begin());
    if (i >= n) return std::nullopt;
    const double length = vertical ? m_area.height() : m_area.width(), start = vertical ? m_area.y() : m_area.x();
    const double from = start + length * double(i) / double(n), size = length / double(n);
    switch (edge) {
    case krellis::Edge::Left: return TRect{m_area.x(), from, m_drawerStripPx, size};
    case krellis::Edge::Right: return TRect{m_area.right() - m_drawerStripPx, from, m_drawerStripPx, size};
    case krellis::Edge::Top: return TRect{from, m_area.y(), size, m_drawerStripPx};
    case krellis::Edge::Bottom: return TRect{from, m_area.bottom() - m_drawerStripPx, size, m_drawerStripPx};
    }
    return std::nullopt;
}

std::optional<TRect> KrellisCanvas::drawerTab(const std::string &panel) const
{
    const auto d = m_drawers.find(panel);
    const auto slice = d != m_drawers.end() && !d->second.open ? drawerSlice(panel) : std::nullopt;
    if (!slice) return std::nullopt;
    const bool vertical = d->second.edge == krellis::Edge::Left || d->second.edge == krellis::Edge::Right;
    // Centred on what the window itself covers of its slice.
    double from = vertical ? slice->y : slice->x, size = vertical ? slice->h : slice->w;
    for (const auto &f : m_doc.floating) {
        if (f.panel->id != panel) continue;
        const double lo = vertical ? m_area.y() + f.rect.y * m_area.height() : m_area.x() + f.rect.x * m_area.width();
        const double hi = lo + (vertical ? f.rect.h * m_area.height() : f.rect.w * m_area.width());
        const double a = std::max(from, lo), b = std::min(from + size, hi);
        if (b > a) { from = a; size = b - a; }
    }
    const double len = std::min(size, m_drawerTabLen), at = from + (size - len) / 2;
    switch (d->second.edge) {
    case krellis::Edge::Left: return TRect{m_area.x(), at, m_drawerTabPx, len};
    case krellis::Edge::Right: return TRect{m_area.right() - m_drawerTabPx, at, m_drawerTabPx, len};
    case krellis::Edge::Top: return TRect{at, m_area.y(), len, m_drawerTabPx};
    case krellis::Edge::Bottom: return TRect{at, m_area.bottom() - m_drawerTabPx, len, m_drawerTabPx};
    }
    return std::nullopt;
}

// Tabs are for choosing: they show while every drawer is closed and at rest, and give way to an open one.
void KrellisCanvas::updateDrawerTabs()
{
    QVariantList tabs;
    const bool anyOpen = std::any_of(m_drawers.begin(), m_drawers.end(), [](const auto &d) { return d.second.open; });
    if (!anyOpen)
        for (const auto &t : m_windows) {
            const auto d = m_drawers.find(t.panel);
            if (d == m_drawers.end() || m_flights.count(t.window) || m_landings.count(t.window)) continue;
            const auto tab = drawerTab(t.panel);
            if (!tab) continue;
            const auto tile = m_tiles.find(t.panel);
            tabs << QVariantMap{{"x", tab->x - m_area.x()}, {"y", tab->y - m_area.y()}, {"w", tab->w}, {"h", tab->h}, {"edge", int(d->second.edge)},
                                {"icon", tile != m_tiles.end() ? tile->second.icon : QString()}, {"title", t.window->caption()}};
        }
    if (tabs.isEmpty()) {
        if (m_drawerTabs) {
            effects->addRepaint(Region(m_drawerTabs->geometry()));
            m_drawerTabs.reset();
            m_drawerTabsSignature.clear();
        }
        return;
    }
    if (!m_drawerTabs) {
        m_drawerTabs = makeScene(QStringLiteral("drawertabs.qml"));
        if (!m_drawerTabs || !m_drawerTabs->rootItem()) {
            qCWarning(krellisLog) << "drawertabs.qml failed to load";
            m_drawerTabs.reset();
            return;
        }
        m_drawerTabsSignature.clear();
    }
    m_drawerTabs->setGeometry(Rect(m_area.toAlignedRect()));
    QString signature;
    for (const auto &v : tabs) {
        const auto m = v.toMap();
        signature += QStringLiteral("%1,%2,%3,%4,%5,%6,%7;").arg(m.value("x").toDouble()).arg(m.value("y").toDouble()).arg(m.value("w").toDouble()).arg(m.value("h").toDouble())
                         .arg(m.value("edge").toInt()).arg(m.value("title").toString(), QString::number(m.value("icon").toString().size()));
    }
    if (signature == m_drawerTabsSignature) return;
    m_drawerTabsSignature = signature;
    m_drawerTabs->rootItem()->setProperty("tabs", tabs);
    effects->addRepaint(Region(m_drawerTabs->geometry()));
}

TRect KrellisCanvas::screenRect(const std::string &panel) const
{
    if (const auto f = floatScreen(panel))
        return *f;
    const TRect *w = worldRect(panel);
    if (!w)
        return {};
    return panelAt(m_tween.apply(panel, *w), m_camera.value);
}

// The camera that client sizes follow: the animated one while it glides, so a window is asked for the
// size it is being shown at rather than jumping to its final size.
TRect KrellisCanvas::sizingCamera() const
{
    return !m_session && !m_resize && m_camera.moving() ? m_camera.value : m_camera.target;
}

TRect KrellisCanvas::restRect(const std::string &panel, const TRect *world) const
{
    TRect r;
    if (const auto floating = floatScreen(panel)) r = *floating;
    else if (world) r = panelAt(*world, sizingCamera());
    else {
        const Entry *entry = m_rects.find(panel);
        if (!entry) return {};
        r = panelAt(entry->rect, sizingCamera());
    }
    r = belowTabs(panel, r); // a tabbed tile's windows sit under its strip
    // Clients get a sane real size even when the canvas is zoomed far out or in; the paint
    // transform covers the difference.
    for (const auto &t : m_windows) if (t.panel == panel) {
        const auto rule = rulesFor(t.window);
        if (rule.scaling != krellis::ViewScaling::None) {
            r.w = std::min(r.w, m_area.width() * 2);
            r.h = std::min(r.h, m_area.height() * 2);
            // A tile smaller than the client can be (the rule's minimum, or what the app itself refuses to go
            // under) asks for the whole tile scaled up evenly, not one axis stretched: the paint transform
            // shrinks it back to the tile, so the contents keep their proportions and fill it.
            const auto floor = clientMinimum(t.window);
            if (r.w > 0 && r.h > 0) {
                const double up = std::max({1.0, floor.x / r.w, floor.y / r.h});
                const double cap = std::max(1.0, MaxHeldSide / std::max(r.w, r.h)); // keeps buffers a sane size
                r.w *= std::min(up, cap);
                r.h *= std::min(up, cap);
            }
            r.w = std::max(floor.x, r.w); // only differs from the above when the cap bit
            r.h = std::max(floor.y, r.h);
        }
        break;
    }
    return r;
}

void KrellisCanvas::relayout(bool animate)
{
    if (m_unloading) return;
    {
        if (!effects->screens().contains(m_output))
            return;
        const auto g = effects->clientArea(MaximizeArea, m_output);
        const QRectF next(g.x(), g.y(), g.width(), g.height());
        if (next != m_screen && !m_uiScreen.isEmpty()) {
            m_uiFrom = m_uiScreen;
            m_areaChangedMs = m_nowMs;
        }
        m_screen = next;
        if (m_uiScreen.isEmpty())
            m_uiScreen = m_screen;
        m_area = QRectF(g.x(), g.y(), std::max(1.0, double(g.width())), std::max(1.0, double(g.height()) - barHeight()));
        // Floats may reach past the usable area to the screen's edge, under a panel.
        const QRectF full(m_output->geometry());
        m_doc.floatMargin = {std::max(0.0, m_area.x() - full.x()) / m_area.width(), std::max(0.0, m_area.y() - full.y()) / m_area.height(),
                             std::max(0.0, full.right() - m_area.right()) / m_area.width(), std::max(0.0, full.bottom() - m_area.bottom()) / m_area.height()};
    }
    for (auto it = m_drawers.begin(); it != m_drawers.end();) {
        const auto loc = krellis::locatePanel(m_doc, it->first);
        if (loc.where == krellis::PanelLocation::Where::Floating) { ++it; continue; }
        const std::string gone = it->first;
        ++it;
        forgetDrawer(gone);
    }
    syncTabs();
    std::map<std::string, TRect> before;
    for (const auto &kv : m_rects)
        before[kv.first] = m_tween.apply(kv.first, kv.second.rect);
    const auto metrics = layoutMetrics();
    m_rects = m_doc.root ? layoutRects(m_doc.root, &metrics) : Entries();
    if (animate && m_glide && !before.empty())
        m_tween.begin(before, m_nowMs);
    m_nav.sync();
    refreshRects();
    applyRest();
    updateBar();
    updateTiles(0);
    updateDrawerTabs();
    updateTabStrips();
    changed();
    schedule();
}

void KrellisCanvas::applyRest(bool stepping)
{
    if (m_unloading) return;
    QScopedValueRollback<bool> applying(m_applyingRest, true);
    const bool previewing = m_session && m_session->active();
    const auto hidden = [this](const std::string &panel) { // a panel is hidden or somewhere in the layout, so no tree search
        return std::any_of(m_doc.hidden.begin(), m_doc.hidden.end(), [&](const auto &h) { return h.panel->id == panel; });
    };
    for (auto *list : {&m_windows, &m_stowed})
    for (auto &t : *list) {
        Window *window = t.window->window();
        if (!window || t.window->isMinimized() || t.window->isFullScreen() || t.window->isUserMove() || t.window->isUserResize() || hidden(t.panel))
            continue; // a fullscreen window covers its output by KWin's rule, not ours
        // While a drag previews a drop, the other windows really take their previewed size, so the
        // client redraws crisply instead of being stretched; the lifted one keeps its own.
        const TRect *previewed = previewing ? worldRect(t.panel) : nullptr;
        if (previewing && (t.window == m_dragWindow || !previewed || floatScreen(t.panel)))
            continue;
        if (m_nativeMutation) m_nativeMutation(t.window, false);
        window->setKeepAbove(floatScreen(t.panel).has_value());
        // Clients are asked for the size they are being drawn at, so a layout glide (maximize, dock,
        // drop, rule change) never lays their contents out at the final size ahead of the animation.
        TRect stepped;
        if (!previewed)
            if (const TRect *world = worldRect(t.panel)) {
                stepped = m_tween.apply(t.panel, *world);
                previewed = &stepped;
            }
        TRect r = previewed ? restRect(t.panel, previewed) : restRect(t.panel);
        const RectF target(r.x, r.y, r.w, r.h);
        // While gliding, a client is given its next size only once it has landed the previous one (or has
        // sat on it for a while), so a heavy client lays out at its own pace instead of at every frame.
        if (stepping && t.requested && m_nowMs - t.requestedMs < SteppingPatienceMs
            && (std::abs(window->frameGeometry().width() - t.requested->width()) > 1
                || std::abs(window->frameGeometry().height() - t.requested->height()) > 1))
            continue;
        if (!t.requested || !(*t.requested == target)) t.requestedMs = m_nowMs;
        t.requested = target;
        if (!m_glide) schedule(); // repaint when the patience window ends, even if the client never commits
        if (window->moveResizeGeometry() != target)
            window->moveResize(target);
    }
}

bool KrellisCanvas::placedByUs(EffectWindow *w) const
{
    for (const auto &t : m_windows) {
        if (t.window != w) continue;
        if (!t.requested || !w->window()) return false;
        const auto near = [&](const RectF &a) {
            return std::abs(a.x() - t.requested->x()) <= 1 && std::abs(a.y() - t.requested->y()) <= 1
                && std::abs(a.width() - t.requested->width()) <= 1 && std::abs(a.height() - t.requested->height()) <= 1;
        };
        return near(w->window()->frameGeometry()) || near(w->window()->moveResizeGeometry());
    }
    return false;
}

// Append at the end seam so every column keeps an equal 1/n share.
void KrellisCanvas::dockAtEnd(const NodePtr &panel)
{
    if (fillVacancyFor(panel, std::nullopt))
        return;
    if (m_doc.root && m_doc.root->kind == Node::Kind::Split && m_doc.root->axis == Axis::X) {
        const auto spec = seamTarget(m_doc.root, int(m_doc.root->children.size()));
        m_doc.root = applyDockTarget(m_doc.root, spec, panel, uid("split"));
    } else if (m_doc.root) {
        m_doc = insertPanel(m_doc, panel, DockTarget::beside(leafIds(m_doc.root).back(), krellis::Edge::Right));
    } else {
        m_doc.root = panel;
    }
}

// Open beside the tile under the cursor, on the side nearest it, as a drop there would. Near a tile's edge
// that is the edge itself; in its middle the nearer half of the tile's longer side (a drop would tab, a new window never does).
void KrellisCanvas::dockAtPointer(const NodePtr &panel)
{
    const QPointF cursor = effects->cursorPos();
    if (fillVacancyFor(panel, !overUi(cursor) ? std::optional<QPointF>(cursor) : std::nullopt))
        return;
    if (!m_doc.root || !m_area.contains(cursor) || overUi(cursor)) {
        dockAtEnd(panel);
        return;
    }
    const auto metrics = layoutMetrics();
    const auto entries = krellis::layoutRects(m_doc.root, &metrics);
    const QPointF rel = cursor - m_area.topLeft();
    const krellis::Point point = fromScreen({rel.x(), rel.y()});
    for (const auto &id : leafIds(m_doc.root)) {
        const auto *entry = entries.find(id);
        if (!entry || point.x < entry->rect.x || point.x > entry->rect.x + entry->rect.w
            || point.y < entry->rect.y || point.y > entry->rect.y + entry->rect.h)
            continue;
        const auto edge = krellis::pointerSpawnEdge(point, entry->rect, m_area.width(), m_area.height());
        m_doc.root = applyDockTarget(m_doc.root, krellis::tileDropTarget(entries, id, edge), panel, uid("split"));
        return;
    }
    dockAtEnd(panel);
}

// Open beside the focused window: on its right, or below it when the tile is taller than wide. Without a docked
// focused window here (nothing active, a float, another screen) the window goes to the end.
void KrellisCanvas::dockAtFocused(const NodePtr &panel)
{
    if (fillVacancyFor(panel, std::nullopt))
        return;
    std::vector<std::string> panels;
    for (const auto &t : m_windows) panels.push_back(t.panel);
    std::string from = dockedPanelOf(*this, m_doc, activeIndex(), panels);
    // The new window is usually active already but not tracked yet: the one that had focus before it is the target.
    if (from.empty() && !m_lastActivePanel.empty()
        && krellis::locatePanel(m_doc, m_lastActivePanel).where == krellis::PanelLocation::Where::Docked)
        from = m_lastActivePanel;
    if (from.empty() || !m_doc.root) {
        dockAtEnd(panel);
        return;
    }
    const auto metrics = layoutMetrics();
    const auto entries = krellis::layoutRects(m_doc.root, &metrics);
    const auto *entry = entries.find(from);
    if (!entry) {
        dockAtEnd(panel);
        return;
    }
    const auto edge = entry->rect.w * m_area.width() >= entry->rect.h * m_area.height() ? krellis::Edge::Right : krellis::Edge::Bottom;
    m_doc.root = applyDockTarget(m_doc.root, krellis::tileDropTarget(entries, from, edge), panel, uid("split"));
}

void KrellisCanvas::dockAtSpawn(const NodePtr &panel)
{
    switch (m_spawnTarget) {
    case krellis::config::SpawnTarget::Focused: dockAtFocused(panel); break;
    case krellis::config::SpawnTarget::End: dockAtEnd(panel); break;
    case krellis::config::SpawnTarget::Pointer: dockAtPointer(panel); break;
    }
}

void KrellisCanvas::windowAdded(EffectWindow *w)
{
    if (!manageable(w) || trackedFor(w) || stowedFor(w))
        return;
    if (m_resize) finishResize(false);
    const auto rule = rulesFor(w);
    if (rule.singleton) {
        for (const auto &other : m_windows)
            if (appId(other.window) == appId(w)) {
                if (m_nativeMutation) m_nativeMutation(w, true);
                w->minimize(); // Preserve the native client; never close a duplicate.
                effects->activateWindow(other.window);
                return;
            }
    }
    auto placement = rule.placementSet ? rule.placement
        : m_floatFirst ? krellis::ViewPlacement::Floating : krellis::ViewPlacement::Side;
    if (!rule.allows(placement)) {
        if (rule.side) placement = krellis::ViewPlacement::Side;
        else if (rule.stage) placement = krellis::ViewPlacement::Stage;
        else if (rule.floating) placement = krellis::ViewPlacement::Floating;
        else return;
    }
    const int n = ++m_counter;
    Tracked t{w, "p" + std::to_string(n), "w" + std::to_string(n)};
    NodePtr panel = makePanel(t.panel, {t.view}, t.view);
    if (placement == krellis::ViewPlacement::Floating) {
        // Successive floats cascade so a new window doesn't hide the one before it.
        const int k = int(m_doc.floating.size()) % 6;
        m_doc = krellis::floatPanel(m_doc, panel, {0.2 + 0.025 * k, 0.15 + 0.035 * k, 0.6, 0.65}, krellis::FloatingLayer::Overlay);
    } else if (placement == krellis::ViewPlacement::Stage) {
        if (const auto stage = krellis::findStage(m_doc.root)) {
            auto updated = std::make_shared<krellis::Node>(*stage);
            updated->child = stage->child ? krellis::makeSplit(uid("stage-row"), krellis::Axis::X, {stage->child, panel}) : panel;
            m_doc.root = krellis::replaceNode(m_doc.root, stage->id, updated);
        } else {
            auto stageNode = krellis::makeStage(uid("stage"), panel);
            m_doc.root = m_doc.root ? krellis::makeSplit(uid("root"), krellis::Axis::X, {stageNode, m_doc.root}) : stageNode;
        }
    } else {
        dockAtSpawn(panel);
    }
    m_windows.push_back(t);
    if (effects->activeWindow() == w) m_lastActivePanel = t.panel; // KWin activated it before we heard of it
    // A native minimized window must enter the hidden model before the first
    // layout can issue a configure. Hidden clients may not acknowledge resizes.
    if (w->isMinimized()) m_doc = hidePanel(m_doc, t.panel);
    relayout(true);
    if (!w->isMinimized()) {
        m_nav.reveal(t.panel, true);
        if (m_glide && m_nowMs > 0) startFlight(w, FlightKind::Appear, {}, {});
    }
    qCDebug(krellisLog) << "added" << w->caption() << QString::fromStdString(t.panel);
}

void KrellisCanvas::windowClosed(EffectWindow *w)
{
    if (m_resize) finishResize(false);
    if (w->isPopupWindow()) {
        double opacity = 1;
        if (const auto from = m_glide ? visualRect(w, &opacity) : std::nullopt)
            m_closing.emplace(w, Closing{EffectWindowDeletedRef(w), *from, m_nowMs, opacity, EffectWindowVisibleRef(w, EffectWindow::PAINT_DISABLED_BY_MINIMIZE)});
        m_popups.erase(w);
        schedule();
        return;
    }
    if (const Tracked *hidden = stowedFor(w)) { // a tab behind another one: nothing was drawn, only the tab goes
        const std::string view = hidden->view;
        std::erase_if(m_stowed, [w](const Tracked &t) { return t.window == w; });
        if (w == m_dragWindow) {
            m_session.reset();
            m_dragWindow = nullptr;
            m_dragTabView.clear();
            m_tabPress.reset();
        }
        m_doc = closeView(m_doc, view);
        relayout(true);
        updateDock();
        return;
    }
    for (size_t i = 0; i < m_windows.size(); ++i) {
        if (m_windows[i].window != w)
            continue;
        // Capture the paint state before dropping a flight (including a hide
        // visible-ref). Native minimized state alone cannot tell us what is drawn.
        double opacity = 1;
        const auto from = visualRect(w, &opacity);
        if (m_glide && from && from->w > 0 && from->h > 0 && opacity > 0 && m_nowMs > 0
            && ((!w->isMinimized() && !w->isHidden()) || m_flights.count(w)))
            m_closing.emplace(w, Closing{EffectWindowDeletedRef(w), *from, m_nowMs, opacity, EffectWindowVisibleRef(w, EffectWindow::PAINT_DISABLED_BY_MINIMIZE)});
        m_flights.erase(w);
        m_landings.erase(w);
        if (w == m_dragWindow) {
            m_session.reset();
            m_dragWindow = nullptr;
        }
        // Closing the window you are on hands focus to its neighbour in the layout, not to whatever KWin last
        // stacked; KWin picks its own successor after this signal, so the hand-over waits one event-loop turn.
        std::string next;
        if (m_closeFocus && (effects->activeWindow() == w || m_lastActivePanel == m_windows[i].panel))
            next = closeFocusPanel(m_doc, m_windows[i].view);
        const std::string panelId = m_windows[i].panel;
        const bool wasActive = effects->activeWindow() == w || m_lastActivePanel == panelId;
        m_doc = closeView(m_doc, m_windows[i].view);
        m_windows.erase(m_windows.begin() + long(i));
        // The tile outlives its shown tab when other tabs are left: focus goes to the tab that takes over.
        if (wasActive && locatePanel(m_doc, panelId).where != PanelLocation::Where::None) next = panelId;
        relayout(true);
        updateDock();
        if (!next.empty())
            QTimer::singleShot(0, this, [this, next] {
                if (Tracked *t = trackedForPanel(next); t && !t->window->isMinimized())
                    effects->activateWindow(t->window);
            });
        return;
    }
}

void KrellisCanvas::windowActivated(EffectWindow *w)
{
    Tracked *t = trackedFor(w);
    if (!t && stowedFor(w)) { // something asked for a tab that is behind another (a launcher, the app itself): bring it forward
        selectTab(stowedFor(w)->view, false);
        t = trackedFor(w);
    }
    if (!t)
        return;
    m_lastActivePanel = t->panel;
    // Whoever asked for this window (the start menu, a launcher, the app itself) wants to see it: a stashed one slides out.
    slideOutDrawer(t->panel);
    if (floatScreen(t->panel))
        m_doc = raiseFloat(m_doc, t->panel);
    else
        m_nav.reveal(t->panel);
}

void KrellisCanvas::updateDock()
{
    m_dockOrder.clear();
    for (const auto &t : m_windows)
        if (t.window->isMinimized() && !scratchWindow(t.window)) // the scratchpad has its own shortcut and minimap entry
            m_dockOrder.push_back(t.window);
    updateBar();
}

static bool sceneContains(const std::unique_ptr<OffscreenQuickScene> &scene, QPointF p)
{
    if (!scene || !QRectF(QRect(scene->geometry())).contains(p))
        return false;
    // A scene may keep transparent room around what it shows (the minimap's help sheet) and name the part
    // that takes input; resizing a live scene does not re-render it, so it is sized for the largest state.
    const QVariant input = scene->rootItem()->property("inputRect");
    if (!input.isValid())
        return true;
    return input.toRectF().contains(p - QPointF(scene->geometry().x(), scene->geometry().y()));
}

static QString sceneKey(const std::unique_ptr<OffscreenQuickScene> &scene, QPointF p)
{
    if (!sceneContains(scene, p))
        return {};
    const QPointF local = p - QPointF(scene->geometry().x(), scene->geometry().y());
    QVariant out;
    QMetaObject::invokeMethod(scene->rootItem(), "hit", Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, local.x()), Q_ARG(QVariant, local.y()),
                              Q_ARG(QVariant, QVariant()));
    return out.toString();
}

bool KrellisCanvas::overUi(QPointF p) const { return !m_overlaysCovered && (sceneContains(m_bar, p) || (m_mapOn && sceneContains(m_map, p))); }

QString KrellisCanvas::barKey(QPointF pointer) const
{
    if (m_mapOn && sceneContains(m_map, pointer))
        return sceneKey(m_map, pointer);
    return sceneKey(m_bar, pointer);
}

double KrellisCanvas::barHeight() const { return m_barOn ? BAR_H : 0; }

void KrellisCanvas::setBarOn(bool on)
{
    if (m_barOn == on) return;
    m_barOn = on;
    m_barSignature.clear();
    updateBar();
    relayout(true);
    schedule();
}

void KrellisCanvas::toggleMap()
{
    m_mapOn = !m_mapOn;
    m_barSignature.clear();
    updateBar();
    schedule();
}

void KrellisCanvas::updateBar()
{
    // Naming a view needs the text field, so the bar floats over the windows for that long without reserving space.
    if (!m_barOn && !m_naming && m_hint.isEmpty()) {
        if (m_bar) {
            effects->addRepaint(Region(m_bar->geometry()));
            m_bar.reset();
        }
        m_barSignature.clear();
        updateMap();
        return;
    }
    if (!m_bar) {
        m_bar = makeScene(QStringLiteral("bar.qml"));
        if (!m_bar || !m_bar->rootItem()) {
            qCWarning(krellisLog) << "bar.qml failed to load";
            m_bar.reset();
            return;
        }
    }
    const QRect geo(int(m_uiScreen.x()), int(m_uiScreen.bottom() - BAR_H), int(m_uiScreen.width()), int(BAR_H));
    QVariantList framings;
    for (const auto &f : m_nav.framings()) {
        QVariantMap m;
        m[QStringLiteral("id")] = QString::fromStdString(f.id);
        m[QStringLiteral("name")] = QString::fromStdString(f.name);
        m[QStringLiteral("current")] = !f.frame.empty() && f.frame.front() == m_nav.framed();
        framings << m;
    }
    QStringList labels;
    for (EffectWindow *w : m_dockOrder)
        labels << w->caption();
    const int hidden = int(m_dockOrder.size());
    const int scratch = int(std::count_if(m_windows.begin(), m_windows.end(), [this](const Tracked &w) { return scratchWindow(w.window); }));
    const int open = int(m_windows.size()) - hidden - scratch;
    const QString count = QStringLiteral("%1 %2").arg(open).arg(open == 1 ? QStringLiteral("window") : QStringLiteral("windows"))
        + (hidden ? QStringLiteral(" · %1 in Dock").arg(hidden) : QString());
    QQuickItem *item = m_bar->rootItem();
    item->setProperty("naming", m_naming);
    item->setProperty("hint", m_hint);
    item->setProperty("nameError", m_nameError);
    const QString signature = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
                                  .arg(geo.x()).arg(geo.y()).arg(geo.width())
                                  .arg(framings.size() ? QString::number(qHash(QVariant(framings).toString())) : QString(), labels.join(QLatin1Char(',')), count)
                                  .arg(m_nav.canGoBack() * 8 + m_nav.canGoForward() * 4 + m_mapOn * 2 + m_nav.overviewing())
                                  .arg(m_hoverKey + m_pressKey, m_hint);
    if (signature == m_barSignature && m_bar->geometry() == Rect(geo))
        return;
    m_barSignature = signature;
    const Rect old = m_bar->geometry();
    m_bar->setGeometry(Rect(geo));
    item->setProperty("framings", framings);
    item->setProperty("labels", labels);
    item->setProperty("overviewCurrent", m_nav.overviewing());
    item->setProperty("canBack", m_nav.canGoBack());
    item->setProperty("canForward", m_nav.canGoForward());
    item->setProperty("mapOn", m_mapOn);
    item->setProperty("countText", count);
    item->setProperty("hoverKey", m_hoverKey);
    item->setProperty("pressKey", m_pressKey);
    effects->addRepaint(Region(old) + Region(Rect(geo)));
    updateMap();
}

static const char *const TINTS[] = {"#3f8fd6", "#d8ab35", "#2f8fe6", "#d65b56", "#b9a7d8", "#2bb45f", "#c97bd1", "#e08a4a"};

// The minimap lives above the bar, bottom right. Tiles refresh when the layout changes; the camera
// frame is fed every frame so zoom and pan gestures are tracked live.
void KrellisCanvas::updateMap(bool force)
{
    if (!m_mapOn && !m_map)
        return;
    if (!m_map) {
        m_map = makeScene(QStringLiteral("minimap.qml"));
        if (!m_map || !m_map->rootItem()) {
            qCWarning(krellisLog) << "minimap.qml failed to load";
            m_map.reset();
            return;
        }
        force = true;
    }
    QQuickItem *item = m_map->rootItem();
    if (!m_mapOn && item->property("settled").toBool()) {
        const Rect g = m_map->geometry();
        m_map.reset();
        effects->addRepaint(Region(g));
        return;
    }
    const double aspect = m_area.height() / std::max(1.0, m_area.width());
    QVariantList tiles;
    QStringList sig;
    const std::string framed = m_nav.framed();
    int i = 0;
    for (const auto &[id, entry] : m_nav.entries()) {
        if (entry.node->kind != Node::Kind::Panel)
            continue;
        QVariantMap m;
        const TRect r = m_tween.apply(id, entry.rect);
        m[QStringLiteral("id")] = QString::fromStdString(id);
        m[QStringLiteral("x")] = r.x;
        m[QStringLiteral("y")] = r.y;
        m[QStringLiteral("w")] = r.w;
        m[QStringLiteral("h")] = r.h;
        m[QStringLiteral("focused")] = framed == id;
        m[QStringLiteral("floating")] = false;
        m[QStringLiteral("vacant")] = isVacant(entry.node);
        m[QStringLiteral("tint")] = QString::fromLatin1(TINTS[i++ % 8]);
        tiles << m;
        sig << QStringLiteral("%1:%2,%3,%4,%5,%6").arg(QString::fromStdString(id)).arg(r.x, 0, 'f', 3).arg(r.y, 0, 'f', 3).arg(r.w, 0, 'f', 3).arg(r.h, 0, 'f', 3).arg(framed == id) + (isVacant(entry.node) ? QStringLiteral("v") : QString());
    }
    for (const auto &f : m_doc.floating) {
        if (std::any_of(m_windows.begin(), m_windows.end(), [&](const Tracked &w) { return w.panel == f.panel->id && scratchWindow(w.window); }))
            continue; // the scratchpad is summoned from the footer, not parked as a tile
        QVariantMap m;
        m[QStringLiteral("id")] = QString::fromStdString(f.panel->id);
        const TRect shown = drawerMapRect(f.panel->id, f.rect);
        m[QStringLiteral("x")] = shown.x;
        m[QStringLiteral("y")] = shown.y;
        m[QStringLiteral("w")] = shown.w;
        m[QStringLiteral("h")] = shown.h;
        m[QStringLiteral("focused")] = false;
        m[QStringLiteral("floating")] = true;
        m[QStringLiteral("tint")] = QString::fromLatin1(TINTS[i++ % 8]);
        tiles << m;
        sig << QStringLiteral("f%1:%2,%3,%4,%5").arg(QString::fromStdString(f.panel->id)).arg(shown.x, 0, 'f', 3).arg(shown.y, 0, 'f', 3).arg(shown.w, 0, 'f', 3).arg(shown.h, 0, 'f', 3);
    }
    const QVariantMap scratch = m_scratchState ? m_scratchState() : QVariantMap();
    sig << QStringLiteral("s%1,%2,%3,%4,%5").arg(scratch.value(QStringLiteral("enabled")).toBool()).arg(scratch.value(QStringLiteral("count")).toInt()).arg(scratch.value(QStringLiteral("shown")).toBool())
               .arg(scratch.value(QStringLiteral("active")).toBool()).arg(scratch.value(QStringLiteral("canMark")).toBool());
    const QString signature = sig.join(QLatin1Char(';')) + QStringLiteral("|%1|%2").arg(aspect, 0, 'f', 3).arg(m_hoverKey + (m_floatFirst ? QStringLiteral("F") : QStringLiteral("T")) + (m_mapPinned ? QStringLiteral("P") : QStringLiteral("U")))
        + (m_mapDragging ? QStringLiteral("|%1,%2,%3,%4|%5").arg(m_mapBand.x()).arg(m_mapBand.y()).arg(m_mapBand.width()).arg(m_mapBand.height()).arg(m_mapPicks.size()) : QString());
    if (force || signature != m_mapSignature) {
        m_mapSignature = signature;
        item->setProperty("aspect", aspect);
        item->setProperty("tiles", tiles);
        item->setProperty("hoverKey", m_hoverKey);
        item->setProperty("stateText", m_nav.overviewing() ? QStringLiteral("Overview") : QStringLiteral("Framed"));
        item->setProperty("floatFirst", m_floatFirst);
        item->setProperty("pinned", m_mapPinned);
        item->setProperty("scratchEnabled", scratch.value(QStringLiteral("enabled")));
        item->setProperty("scratchCount", scratch.value(QStringLiteral("count")));
        item->setProperty("scratchShown", scratch.value(QStringLiteral("shown")));
        item->setProperty("scratchActive", scratch.value(QStringLiteral("active")));
        item->setProperty("scratchCanMark", scratch.value(QStringLiteral("canMark")));
        item->setProperty("band", m_mapBand);
        item->setProperty("bandOn", m_mapDragging);
        QStringList picks;
        for (const auto &id : m_mapPicks)
            picks << QString::fromStdString(id);
        item->setProperty("picks", picks);
    }
    const TRect c = m_camera.value;
    item->setProperty("camX", c.x);
    item->setProperty("camY", c.y);
    item->setProperty("camW", c.w);
    item->setProperty("camH", c.h);
    item->setProperty("shown", m_mapOn);
    const QSizeF size(item->implicitWidth(), item->implicitHeight());
    // Flush with the screen edge: the card slides off it when the map auto-hides.
    const QRect geo(int(m_uiScreen.right() - size.width()), int(m_uiScreen.bottom() - barHeight() - size.height() - 8), int(size.width()), int(size.height()));
    const Rect old = m_map->geometry();
    if (old != Rect(geo))
        m_map->setGeometry(Rect(geo));
    effects->addRepaint(Region(old) + Region(Rect(geo)));
}

bool KrellisCanvas::mapSelectKey(const QString &key) const
{
    return m_mapOn && m_map && (key == QLatin1String("map:overview") || key.startsWith(QLatin1String("map:frame:")) || key.startsWith(QLatin1String("map:focus:")));
}

// Drag a square on the minimap: the tiles it touches are what gets framed on release.
void KrellisCanvas::moveMapSelect(QPointF pointer)
{
    const QRectF stage = m_map->rootItem()->property("stageRect").toRectF();
    const QPointF origin(m_map->geometry().x(), m_map->geometry().y());
    const auto toStage = [&](QPointF p) {
        const QPointF q = p - origin - stage.topLeft();
        return QPointF(std::clamp(q.x(), 0.0, stage.width()), std::clamp(q.y(), 0.0, stage.height()));
    };
    m_mapBand = QRectF(toStage(m_mapAnchor), toStage(pointer)).normalized();
    m_mapPicks.clear();
    if (stage.width() > 0 && stage.height() > 0) {
        // Tiles are drawn inset by their gap, so only a square that reaches the drawn tile counts.
        const double gx = 1.5 / stage.width(), gy = 1.5 / stage.height();
        const double bx0 = m_mapBand.left() / stage.width(), bx1 = m_mapBand.right() / stage.width();
        const double by0 = m_mapBand.top() / stage.height(), by1 = m_mapBand.bottom() / stage.height();
        for (const auto &[id, entry] : m_nav.entries()) {
            if (entry.node->kind != Node::Kind::Panel)
                continue;
            const auto &r = entry.rect;
            if (bx1 > r.x + gx && bx0 < r.x + r.w - gx && by1 > r.y + gy && by0 < r.y + r.h - gy)
                m_mapPicks.push_back(id);
        }
    }
    updateMap(true);
    schedule();
}

void KrellisCanvas::finishMapSelect(bool commit)
{
    const auto picks = std::move(m_mapPicks);
    m_mapDragging = false;
    m_mapPicks.clear();
    m_mapBand = QRectF();
    if (commit && !picks.empty()) {
        m_nav.frameTiles(picks);
        applyRest();
    }
    updateMap(true);
    schedule();
}

void KrellisCanvas::barAction(const QString &key)
{
    if (key == QLatin1String("overview")) {
        m_nav.overview();
    } else if (key == QLatin1String("back")) {
        m_nav.back();
    } else if (key == QLatin1String("forward")) {
        m_nav.forward();
    } else if (key == QLatin1String("save")) {
        if (m_naming) submitFramingName();
        else beginFramingName();
    } else if (key == QLatin1String("name-cancel")) {
        m_naming = false;
        m_nameError = false;
    } else if (key == QLatin1String("map")) {
        m_mapOn = !m_mapOn;
    } else if (key == QLatin1String("map:pin")) {
        if (m_togglePin)
            m_togglePin();
    } else if (key == QLatin1String("map:mode")) {
        if (m_toggleMode)
            m_toggleMode();
    } else if (key == QLatin1String("map:scratch") || key == QLatin1String("map:scratch-mark")) {
        if (m_scratchAction)
            m_scratchAction(key == QLatin1String("map:scratch") ? QStringLiteral("toggle") : QStringLiteral("mark"));
    } else if (key == QLatin1String("map:overview")) {
        m_nav.overview();
    } else if (key.startsWith(QLatin1String("map:frame:"))) {
        // Clicking the tile that is already framed zooms back out a level instead of re-framing it.
        const std::string id = key.mid(10).toStdString();
        if (m_nav.framed() == id && !m_nav.boxed())
            m_nav.stepOut();
        else
            m_nav.focus(id);
    } else if (key.startsWith(QLatin1String("map:focus:"))) {
        if (const auto *t = [&]() -> const Tracked * {
                for (const auto &w : m_windows)
                    if (w.panel == key.mid(10).toStdString())
                        return &w;
                return nullptr;
            }()) {
            // A drawer's sliver on the map slides it out, and keeps it out while the pointer is off at the map.
            slideOutDrawer(t->panel);
            effects->activateWindow(t->window);
        }
    } else if (key.startsWith(QLatin1String("framing:"))) {
        m_nav.goFraming(key.mid(8).toStdString());
    } else if (key.startsWith(QLatin1String("rm:"))) {
        m_nav.removeFraming(key.mid(3).toStdString());
    } else if (key.startsWith(QLatin1String("dock:"))) {
        const int i = key.mid(5).toInt();
        if (i >= 0 && i < int(m_dockOrder.size()))
            m_dockOrder[size_t(i)]->unminimize();
    }
    applyRest();
    updateBar();
    schedule();
}

TRect KrellisCanvas::chipRect(EffectWindow *w) const
{
    const auto it = std::find(m_dockOrder.begin(), m_dockOrder.end(), w);
    if (!m_bar || it == m_dockOrder.end())
        return {m_area.center().x() - 60, m_screen.bottom() - 40, 120, 30};
    QVariant out;
    QMetaObject::invokeMethod(m_bar->rootItem(), "chipRect", Q_RETURN_ARG(QVariant, out),
                              Q_ARG(QVariant, int(it - m_dockOrder.begin())));
    const QRectF r = out.toRectF();
    const Rect g = m_bar->geometry();
    if (r.isEmpty())
        return {double(g.x()), double(g.y()), double(g.width()), double(g.height())};
    return {g.x() + r.x(), g.y() + r.y(), r.width(), r.height()};
}

void KrellisCanvas::startFlight(EffectWindow *w, FlightKind kind, TRect from, TRect to, double opacity)
{
    Flight f{kind, from, to, m_nowMs, 0, false, std::nullopt};
    f.fromOpacity = opacity;
    f.duration = kind == FlightKind::Appear ? APPEAR_MS : kind == FlightKind::Hide ? krellis::DOCK_HIDE_MS : kind == FlightKind::Glide ? krellis::Motion::layoutMs : krellis::DOCK_RESTORE_MS;
    if (!m_glide)
        f.duration = 1; // keeps the flight bookkeeping (hide refs, native restore) but lands on the next frame
    if (kind == FlightKind::Hide)
        f.ref.emplace(w, EffectWindow::PAINT_DISABLED_BY_MINIMIZE);
    m_flights.erase(w);
    m_flights.emplace(w, std::move(f));
    schedule();
}

// The animated screen rect and opacity for a window in flight. False when it is not flying.
bool KrellisCanvas::flightRect(EffectWindow *w, const Tracked &t, TRect &rect, double &opacity) const
{
    const auto it = m_flights.find(w);
    if (it == m_flights.end())
        return false;
    const Flight &f = it->second;
    const double p = std::min(1.0, std::max(0.0, (m_nowMs - f.start) / f.duration));
    switch (f.kind) {
    case FlightKind::Appear: {
        const double e = APPEAR_EASE(p);
        const TRect r = belowTabs(t.panel, screenRect(t.panel));
        const double s = 0.97 + 0.03 * e;
        rect = {r.x + r.w * (1 - s) / 2, r.y + r.h * (1 - s) / 2, r.w * s, r.h * s};
        opacity = e;
        return true;
    }
    case FlightKind::Hide: {
        const double e = krellis::DOCK_EASE(p);
        rect = krellis::lerpRect(f.from, f.to, e);
        // An acknowledgement hold is native visibility only. The visual hide
        // still ends at 300 ms, with no translucent card left over the dock.
        opacity = p >= 1 && f.nativeRestore ? 0 : f.fromOpacity + (0.1 - f.fromOpacity) * e;
        return true;
    }
    case FlightKind::Glide: {
        rect = krellis::lerpRect(f.from, shownRect(t.panel), krellis::easeOutQuint(p));
        opacity = f.fromOpacity + (1 - f.fromOpacity) * krellis::easeOutQuint(p);
        return true;
    }
    case FlightKind::Restore: {
        const double e = krellis::DOCK_EASE(p);
        rect = krellis::lerpRect(f.from, belowTabs(t.panel, screenRect(t.panel)), e);
        opacity = f.fromOpacity + (1 - f.fromOpacity) * e;
        return true;
    }
    }
    return false;
}

void KrellisCanvas::minimizedChanged(EffectWindow *w)
{
    if (m_resize) finishResize(false);
    Tracked *t = trackedFor(w);
    if (!t)
        return;
    const bool hidden = krellis::locatePanel(m_doc, t->panel).where == krellis::PanelLocation::Where::Hidden;
    if (w->isMinimized() && !hidden) {
        double opacity = 1;
        const TRect from = visualRect(w, &opacity).value_or(screenRect(t->panel));
        m_doc = hidePanel(m_doc, t->panel);
        relayout(true);
        updateDock();
        startFlight(w, FlightKind::Hide, from, chipRect(w), opacity);
        // Restore the baseline while the hide flight's visible ref keeps the
        // client unsuspended and able to grow its buffer. Once fully hidden,
        // a configure-only commit can clip a larger target to the old buffer.
        if (m_hiddenNativeRect && w->window()) {
            if (const auto baseline = m_hiddenNativeRect(w)) {
                // Only restore its size here. Native output ownership follows
                // the frame's centre, so keep that centre on this canvas even
                // when the original window was larger than the current output.
                const RectF target(m_area.center().x() - baseline->width() / 2,
                                   m_area.center().y() - baseline->height() / 2,
                                   baseline->width(), baseline->height());
                auto &flight = m_flights.at(w);
                flight.nativeRestore = target;
                QScopedValueRollback<bool> applying(m_applyingRest, true);
                w->window()->moveResize(target);
            }
        }
    } else if (!w->isMinimized() && hidden) {
        double opacity = 0.1;
        TRect from = chipRect(w);
        // A completed hidden window has no painted rect; an interrupted hide does.
        if (m_flights.count(w))
            from = visualRect(w, &opacity).value_or(from);
        m_doc = restorePanel(m_doc, t->panel, krellis::FloatingLayer::Overlay);
        relayout(true);
        m_nav.reveal(t->panel);
        applyRest();
        updateDock();
        startFlight(w, FlightKind::Restore, from, {}, opacity);
    }
}

void KrellisCanvas::toggleHide(int i)
{
    if (i < 0 || i >= int(m_windows.size()))
        return;
    EffectWindow *w = m_windows[size_t(i)].window;
    if (w->isMinimized())
        w->unminimize();
    else
        w->minimize();
}

void KrellisCanvas::prePaintScreen(ScreenPrePaintData &data)
{
    const auto ms = m_clock.tick(data.view).count();
    m_nowMs += double(ms);
    // The scenes live in KWin's overlay item, above every window, a fullscreen one included.
    m_overlaysCovered = std::any_of(m_windows.begin(), m_windows.end(), [](const Tracked &t) { return t.window->isFullScreen() && !t.window->isMinimized(); });
    if (m_overlaysCovered && m_paletteOn) closePalette(); // it would sit hidden over a window that keeps the keyboard
    updatePins();
    for (auto *scene : {&m_bar, &m_map, &m_tileScene, &m_drawerTabs, &m_tabStrips, &m_slot, &m_toast, &m_sheet, &m_palette, &m_marquee})
        if (*scene) (*scene)->setVisible(!m_overlaysCovered);
    m_layerTarget = layerShown() ? 1 : 0;
    if (m_layerDim != m_layerTarget) {
        const double step = double(ms) / 180.0; // the dim eases in and out in about a fifth of a second
        m_layerDim = m_layerDim < m_layerTarget ? std::min(m_layerTarget, m_layerDim + step) : std::max(m_layerTarget, m_layerDim - step);
        m_animating = true;
    }
    if (m_animating) {
        const double dt = std::min(Motion::maxStep, double(ms) / 1000.0);
        if (m_session) {
            m_session->tick(m_nowMs);
            if (m_session->active()) {
                m_lifted = m_session->liftedRect(m_nowMs);
                applyRest();
            }
        }
        if (m_resize) m_camera.jump(m_resize->camera);
        m_nav.tick(dt, realMs()); // may hand a pending flight its destination
        const bool springing = m_camera.step(dt);
        const bool tweening = m_tween.step(m_nowMs);
        if (!m_session && !m_resize) {
            // Clients follow the glide in real steps (camera moves included), so their contents are
            // never laid out at the final size and squashed into the shrinking or growing rect.
            if (tweening || springing) applyRest(true);
            else if (m_steppingResize) applyRest();
            m_steppingResize = tweening || springing;
        }
        for (auto it = m_flights.begin(); it != m_flights.end();) {
            const auto &flight = it->second;
            const double elapsed = m_nowMs - flight.start;
            bool waitingForBuffer = false;
            if (elapsed >= flight.duration && flight.nativeRestore && it->first->window()) {
                const auto actual = it->first->window()->frameGeometry();
                const auto requested = it->first->window()->moveResizeGeometry();
                const auto wanted = *flight.nativeRestore;
                auto matches = [&wanted](const RectF &rect) {
                    return std::abs(rect.x() - wanted.x()) <= 1 && std::abs(rect.y() - wanted.y()) <= 1
                        && std::abs(rect.width() - wanted.width()) <= 1 && std::abs(rect.height() - wanted.height()) <= 1;
                };
                waitingForBuffer = !matches(actual) || !matches(requested);
                if (waitingForBuffer && elapsed >= flight.duration + 1500) {
                    qCWarning(krellisLog) << "hide native restore timed out" << appId(it->first)
                                         << "target" << wanted << "frame" << actual << "requested" << requested;
                    waitingForBuffer = false;
                }
            }
            if (elapsed >= flight.duration && !waitingForBuffer)
                it = m_flights.erase(it);
            else
                ++it;
        }
        for (auto it = m_closing.begin(); it != m_closing.end();) {
            if (m_nowMs - it->second.start >= CLOSE_MS)
                it = m_closing.erase(it);
            else
                ++it;
        }
        std::erase_if(m_landings, [this](const auto &l) { return m_nowMs - l.second.start >= m_landMs; });
        std::erase_if(m_popups, [this](const auto &p) { return m_nowMs - p.second >= APPEAR_MS; });
        refreshRects();
        updateSlot(dt);
        updateMarquee(dt);
        updateToast();
        updateSheet(dt);
        updatePalette(dt);
        const double areaProgress = std::clamp((m_nowMs - m_areaChangedMs) / krellis::Motion::layoutMs, 0.0, 1.0);
        if (!m_uiFrom.isEmpty()) {
            const double t = krellis::easeOutQuint(areaProgress);
            m_uiScreen = QRectF(m_uiFrom.topLeft() + (m_screen.topLeft() - m_uiFrom.topLeft()) * t,
                                m_uiFrom.size() + (m_screen.size() - m_uiFrom.size()) * t);
            updateBar();
        }
        updateTiles(dt);
        updateMap();
        updateDrawerTabs();
        updateTabStrips();
        const bool tilesMoving = std::any_of(m_tiles.begin(), m_tiles.end(), [](const auto &t) { return std::abs(t.second.velocity) > 0.001 || std::abs(t.second.target - t.second.amount) > 0.0001; });
        m_animating = tilesMoving || areaProgress < 1 || springing || (m_map && !m_map->rootItem()->property("settled").toBool()) || tweening || !m_flights.empty() || !m_popups.empty() || !m_closing.empty() || m_session || m_slot || m_marquee || m_nav.flying() || m_toastBusy || m_sheetBusy || m_paletteBusy || m_layerDim != m_layerTarget || (!m_glide && layoutAwaitingLanding()) || !m_landings.empty();
    }
}

void KrellisCanvas::postPaintScreen()
{
    if (m_animating)
        effects->addRepaintFull();
}

void KrellisCanvas::prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data)
{
    if (visualRect(w)) {
        data.setTransformed();
        if (m_flights.count(w) || w->isPopupWindow())
            data.setTranslucent();
        if (const auto *t = trackedFor(w))
            if (const auto tile = m_tiles.find(t->panel); tile != m_tiles.end() && tile->second.amount > 0)
                data.setTranslucent();
    } else if (m_closing.count(w) || behindTab(w)) {
        data.setTransformed();
        data.setTranslucent();
    }
}

void KrellisCanvas::paintWindow(const RenderTarget &rt, const RenderViewport &vp, EffectWindow *w, int mask, const Region &region, WindowPaintData &data)
{
    double opacity = 1;
    const auto drawn = visualRect(w, &opacity);
    if (!drawn && behindTab(w)) return; // a tab behind another one: it holds the tile's size, not its place in the picture
    if (drawn) {
        const TRect want = *drawn;
        if (auto *tracked = trackedFor(w)) tracked->lastDrawn = want;
        if (const auto *tracked = trackedFor(w))
            if (const auto tile = m_tiles.find(tracked->panel); tile != m_tiles.end())
                opacity *= 1 - std::clamp(tile->second.amount, 0.0, 1.0);
        data.multiplyOpacity(opacity);
        if (m_layerDim > 0 && !scratchWindow(w)) { // under the scratch layer: still there, but not asking for attention
            data.setBrightness(data.brightness() * (1 - 0.42 * m_layerDim));
            data.setSaturation(data.saturation() * (1 - 0.3 * m_layerDim));
        }
        if (const auto *tracked = trackedFor(w); tracked && peeking(tracked->panel)) {
            data.setBrightness(0.55); // out of focus: the edge is there to be found, not to be read
            data.setSaturation(0.6);
        }
        const RectF frame = w->frameGeometry();
        if (want.w > 0 && want.h > 0 && frame.width() > 0 && frame.height() > 0) {
            data += QPointF(want.x - frame.x(), want.y - frame.y());
            data *= QVector2D(float(want.w / frame.width()), float(want.h / frame.height()));
        }
    }
    else if (const auto c = m_closing.find(w); c != m_closing.end()) {
        // Fade out while easing down to 97% about the centre: the appear curve, played backwards.
        const double p = std::min(1.0, std::max(0.0, (m_nowMs - c->second.start) / CLOSE_MS));
        const double e = APPEAR_EASE(1 - p);
        const TRect r = c->second.rect;
        const double s = 0.97 + 0.03 * e;
        const RectF frame = w->frameGeometry();
        if (frame.width() > 0 && frame.height() > 0) {
            data += QPointF(r.x + r.w * (1 - s) / 2 - frame.x(), r.y + r.h * (1 - s) / 2 - frame.y());
            data *= QVector2D(float(r.w * s / frame.width()), float(r.h * s / frame.height()));
        }
        data.multiplyOpacity(c->second.opacity * e);
    }
    Region clipped = region;
    if (const auto *tracked = trackedFor(w)) {
        const auto d = m_drawers.find(tracked->panel);
        // Not while it slides back in (flight or landing): the clip would cut it down to the sliver at once.
        if (d != m_drawers.end() && !d->second.open && m_drawers.size() > 1 && !m_flights.count(w) && !m_landings.count(w))
            if (const auto slice = drawerSlice(tracked->panel))
                clipped = region & vp.mapToDeviceCoordinatesAligned(Region(RectF(slice->x, slice->y, slice->w, slice->h).toAlignedRect()));
    }
    effects->paintWindow(rt, vp, w, mask, clipped, data);
}

// With glide off, a window whose real geometry just changed plays a short move-and-scale from where it was.
// The content is already laid out at the new size, so nothing is re-flowed mid-animation.
void KrellisCanvas::frameChanged(EffectWindow *w, const RectF &old)
{
    if (m_glide || m_landMs <= 0 || (m_session && w == m_dragWindow) || m_resize || m_flights.count(w) || !trackedFor(w) || w->isFullScreen()) return;
    if (w->isUserMove() || w->isUserResize() || w->isMinimized() || w->isHidden()) return;
    const QRectF now = w->frameGeometry();
    if (old.width() <= 0 || old.height() <= 0 || now.width() <= 0 || now.height() <= 0) return;
    if (std::abs(old.x() - now.x()) < 1 && std::abs(old.y() - now.y()) < 1
        && std::abs(old.width() - now.width()) < 1 && std::abs(old.height() - now.height()) < 1) return;
    // Start from what was last drawn (this also retargets a running glide), so a window that was already
    // shown at its final rect, such as a neighbour stretched during a border drag, does not jump back.
    const Tracked *t = trackedFor(w);
    TRect from = t && t->lastDrawn ? *t->lastDrawn : TRect{old.x(), old.y(), old.width(), old.height()};
    m_landings[w] = {from, m_nowMs};
    schedule();
}

// A client asked for a size it has not yet landed, within the patience window. Clients held at another
// size by rules never land, and are scaled by design instead.
bool KrellisCanvas::awaitingLanding(const Tracked &t) const
{
    const EffectWindow *w = t.window;
    if (!t.requested || w->isMinimized() || w->isHidden() || w->isFullScreen() || m_flights.count(t.window)) return false;
    if (m_nowMs - t.requestedMs >= LandingPatienceMs) return false;
    const QRectF frame = w->frameGeometry();
    if (frame.width() <= 0 || frame.height() <= 0 || t.requested->width() <= 0) return false;
    if (const Entry *entry = m_rects.find(t.panel)) {
        const TRect natural = belowTabs(t.panel, panelAt(entry->rect, sizingCamera())), real = restRect(t.panel);
        if (std::abs(natural.w - real.w) > 0.5 || std::abs(natural.h - real.h) > 0.5) return false;
    }
    return std::abs(frame.width() - t.requested->width()) > 1 || std::abs(frame.height() - t.requested->height()) > 1;
}

bool KrellisCanvas::layoutAwaitingLanding() const
{
    return std::any_of(m_windows.begin(), m_windows.end(), [this](const Tracked &t) { return awaitingLanding(t); });
}

std::optional<TRect> KrellisCanvas::visualRect(EffectWindow *w, double *opacity) const
{
    if (m_closing.count(w)) return std::nullopt;
    if (m_session && m_session->active() && w == m_dragWindow && stowedFor(w)) { // a tab torn out: the one thing of its tile in sight
        if (opacity) *opacity = 1;
        return TRect{m_lifted.x + m_area.x(), m_lifted.y + m_area.y(), m_lifted.w, m_lifted.h};
    }
    for (const auto &t : m_windows) {
        if (t.window != w) continue;
        if (w->isFullScreen()) return std::nullopt; // painted and hit-tested untransformed, as KWin places it
        TRect rect = shownRect(t.panel);
        double alpha = 1;
        if (m_session && m_session->active() && w == m_dragWindow)
            rect = {m_lifted.x + m_area.x(), m_lifted.y + m_area.y(), m_lifted.w, m_lifted.h};
        else
            flightRect(w, t, rect, alpha);
        if (opacity) *opacity = alpha;
        // Never stretch contents: a window in transit keeps its own aspect and is drawn at the
        // largest uniform scale that fits, anchored top-left, until its client catches up.
        // Zoomed views, live divider drags and floats deliberately fit windows to their rect.
        const QRectF frame = w->frameGeometry();
        const bool flights = m_flights.count(w) || (m_session && m_session->active() && w == m_dragWindow);
        const auto &cam = m_camera.value;
        const bool unzoomed = std::abs(cam.w - 1) < 0.01 && std::abs(cam.h - 1) < 0.01;
        const Entry *entry = m_rects.find(t.panel);
        // A client held at a size other than its tile (minimum or maximum rules) is meant to be scaled.
        const bool clamped = entry && [&] {
            const TRect natural = belowTabs(t.panel, panelAt(entry->rect, sizingCamera())), real = restRect(t.panel);
            return std::abs(natural.w - real.w) > 0.5 || std::abs(natural.h - real.h) > 0.5;
        }();
        // Without a glide the new layout is final from the first frame, while clients still hold their old
        // buffers. Rather than stretch those into the new rects (a blink when they land) or leave gaps, hold
        // every window at its real geometry until all the resized clients have landed, then show the new
        // layout at once. Not during a live border drag: a slow client (Brave) held at its stale
        // size would trail the dragged edge, so neighbours follow it, slightly stretched, as before.
        bool lagging = false;
        const bool thumbnail = pinRect(t.panel).has_value(); // sized from the window itself, so nothing to fit
        if (!m_glide && !flights && !thumbnail && frame.width() > 0 && frame.height() > 0 && !m_resize && layoutAwaitingLanding()) {
            rect = {frame.x(), frame.y(), frame.width(), frame.height()};
            lagging = true;
        }
        if (!lagging && !thumbnail && unzoomed && !clamped && !m_resize && !floatScreen(t.panel) && frame.width() > 0 && frame.height() > 0 && rect.w > 0 && rect.h > 0) {
            const double sx = rect.w / frame.width(), sy = rect.h / frame.height();
            if (std::abs(sx - sy) > 0.01 * std::max(sx, sy)) {
                const double u = std::min(sx, sy);
                rect.w = frame.width() * u;
                rect.h = frame.height() * u;
            }
        }
        if (const auto it = m_landings.find(w); it != m_landings.end())
            rect = krellis::lerpRect(it->second.from, rect, krellis::easeOutQuint((m_nowMs - it->second.start) / m_landMs));
        return rect;
    }
    if (w->isPopupWindow()) {
        EffectWindow *parent = w->transientFor();
        if (parent && parent != w) {
            if (const auto shown = visualRect(parent, opacity)) {
                const auto p = parent->frameGeometry();
                const auto r = w->frameGeometry();
                auto rect = krellis::mapChildRect({r.x(), r.y(), r.width(), r.height()},
                    {p.x(), p.y(), p.width(), p.height()}, *shown);
                if (rect) {
                    if (const auto it = m_popups.find(w); it != m_popups.end()) {
                        const double e = APPEAR_EASE(std::clamp((m_nowMs - it->second) / APPEAR_MS, 0.0, 1.0));
                        const double scale = 0.97 + 0.03 * e;
                        rect->x += rect->w * (1 - scale) / 2;
                        rect->y += rect->h * (1 - scale) / 2;
                        rect->w *= scale;
                        rect->h *= scale;
                        if (opacity) *opacity *= e;
                    }
                }
                return rect;
            }
        }
    }
    return std::nullopt;
}

// ------------------------------------------------------------------ input
KrellisInput::KrellisInput(KrellisEffect *effect)
    : InputEventFilter(InputFilterOrder::Effects)
    , m_effect(effect)
{
}
bool KrellisInput::pointerMotion(PointerMotionEvent *e) { return m_effect->handleMotion(*e); }
bool KrellisInput::pointerButton(PointerButtonEvent *e) { return m_effect->handleButton(*e); }
bool KrellisInput::pointerAxis(PointerAxisEvent *e) { return m_effect->handleAxis(*e); }
bool KrellisInput::keyboardKey(KeyboardKeyEvent *e) { return m_effect->handleKey(*e); }
bool KrellisInput::pinchGestureBegin(PointerPinchGestureBeginEvent *e)
{
    return e->fingerCount >= 2;
}
bool KrellisInput::pinchGestureUpdate(PointerPinchGestureUpdateEvent *e)
{
    m_effect->pinch(e->scale, effects->cursorPos(), e->delta);
    return true;
}
bool KrellisInput::pinchGestureEnd(PointerPinchGestureEndEvent *)
{
    m_effect->endPinch();
    return true;
}
bool KrellisInput::pinchGestureCancelled(PointerPinchGestureCancelEvent *)
{
    m_effect->endPinch();
    return true;
}

static constexpr auto GESTURE_MODS = Qt::ControlModifier | Qt::AltModifier;

bool KrellisCanvas::handleKey(const KeyboardKeyEvent &e)
{
    if (m_paletteOn) return paletteKey(e);
    if (m_naming && m_bar && m_bar->rootItem()) {
        if (e.key == Qt::Key_Escape) {
            if (e.state == KeyboardKeyState::Pressed) { m_naming = false; m_nameError = false; updateBar(); }
        } else if (e.key == Qt::Key_Return || e.key == Qt::Key_Enter) {
            if (e.state == KeyboardKeyState::Pressed) submitFramingName();
        } else {
            QKeyEvent key(e.state == KeyboardKeyState::Released ? QEvent::KeyRelease : QEvent::KeyPress,
                e.key, e.modifiers, e.nativeScanCode, e.nativeVirtualKey, 0, e.text,
                e.state == KeyboardKeyState::Repeated);
            m_bar->forwardKeyEvent(&key);
        }
        return true;
    }
    if (e.key == Qt::Key_V)
        m_keyV = e.state != KeyboardKeyState::Released;
    if (e.key == Qt::Key_Escape && m_mapDragging) {
        if (e.state == KeyboardKeyState::Pressed) {
            m_pressBar = false;
            m_pressKey.clear();
            finishMapSelect(false);
        }
        return true;
    }
    // Esc abandons a rectangle selection and returns to the current framing.
    if (e.key == Qt::Key_Escape && m_drag == Drag::Rect) {
        if (e.state == KeyboardKeyState::Pressed) {
            m_drag = Drag::None;
            finishRectSelect(false);
        }
        return true;
    }
    return false;
}

bool KrellisCanvas::handleButton(const PointerButtonEvent &e)
{
    if (handleIconButton(e) || handlePeekButton(e) || tabButton(e)) return true;
    if (m_naming && barKey(e.position) == QLatin1String("name-edit") && m_bar) {
        auto event = e;
        m_bar->forwardPointerButtonEvent(&event);
        return true;
    }
    if (e.button == Qt::MiddleButton) {
        if (e.state == PointerButtonState::Released) {
            const bool ours = m_swallowMiddle;
            m_swallowMiddle = false;
            return ours;
        }
        std::string split;
        int index = 0;
        if (overUi(e.position) || !seamAt(e.position, &split, &index)) return false;
        {
            EditScope edit(*this, "flip split");
            m_doc.root = krellis::flipBoundary(m_doc.root, split, index, krellis::uid("split"));
        }
        m_swallowMiddle = true;
        relayout(true);
        applyRest();
        return true;
    }
    if (e.button != Qt::LeftButton)
        return false;
    if (e.state == PointerButtonState::Released) {
        if (m_pressBar) {
            const QString pressed = m_pressKey;
            m_pressBar = false;
            m_pressKey.clear();
            if (m_mapDragging) {
                finishMapSelect(true);
                return true;
            }
            if (!pressed.isEmpty() && barKey(e.position) == pressed)
                barAction(pressed);
            else
                updateBar();
            return true;
        }
        if (m_drag == Drag::None)
            return false;
        const bool rect = m_drag == Drag::Rect;
        m_drag = Drag::None;
        if (rect) {
            finishRectSelect(true);
            return true;
        }
        m_nav.finishGesture();
        applyRest();
        return true;
    }
    // The bar swallows presses over it; the action fires on release over the same item.
    if (const QString key = barKey(e.position); overUi(e.position)) {
        m_pressKey = key;
        m_pressBar = true;
        m_mapAnchor = e.position;
        m_mapDragging = false;
        updateBar();
        return true;
    }
    // Meta+drag draws the zoom-to-rectangle square; Ctrl+Alt+drag pans.
    const bool square = (e.modifiers & Qt::MetaModifier) && !(e.modifiers & (Qt::ControlModifier | Qt::AltModifier));
    if (square || (e.modifiers & GESTURE_MODS) == GESTURE_MODS) {
        m_dragLast = m_dragAnchor = e.position;
        if (square) {
            m_drag = Drag::Rect;
            m_nav.endGesture();
            m_rectView = m_camera.value;
            moveRectSelect(e.position);
        } else {
            m_drag = m_keyV ? Drag::Scale : Drag::Pan;
        }
        return true;
    }
    return false;
}

bool KrellisCanvas::handleMotion(const PointerMotionEvent &e)
{
    updateDrawerHover(e);
    if (handleIconMotion(e) || tabMotion(e.position)) return true;
    if (m_naming && barKey(e.position) == QLatin1String("name-edit") && m_bar) {
        auto event = e;
        m_bar->forwardPointerMotionEvent(&event);
        return true;
    }
    if (m_pressBar && mapSelectKey(m_pressKey)) {
        if (!m_mapDragging && QLineF(m_mapAnchor, e.position).length() >= 5)
            m_mapDragging = true;
        if (m_mapDragging) {
            moveMapSelect(e.position);
            return true;
        }
    }
    if (m_drag == Drag::None) {
        const QString key = barKey(e.position);
        if (key != m_hoverKey) {
            m_hoverKey = key;
            updateBar();
        }
        return false;
    }
    const QPointF d = e.position - m_dragLast;
    m_dragLast = e.position;
    if (m_drag == Drag::Rect) {
        moveRectSelect(e.position);
        return true;
    }
    if (m_drag == Drag::Pan) {
        m_nav.zoom(1.0, {0.5, 0.5}, {-d.x(), -d.y()});
    } else {
        const QPointF a = m_dragAnchor - m_area.topLeft();
        // Dragging up or right zooms in.
        m_nav.zoom(std::exp(-(d.x() - d.y()) * 0.004), {a.x() / m_area.width(), a.y() / m_area.height()});
    }
    return true;
}

bool KrellisCanvas::handleAxis(const PointerAxisEvent &e, bool titleBar)
{
    if (e.orientation != Qt::Vertical)
        return false;
    // Scrolling zooms one column and row at a time: with Alt or Meta held on a notched wheel (Ctrl is left to
    // the apps, which zoom their contents with it), with Ctrl+Alt on anything, or with nothing held over a
    // title bar.
    const bool chord = (e.modifiers & GESTURE_MODS) == GESTURE_MODS;
    const bool keyWheel = (e.modifiers & (Qt::AltModifier | Qt::MetaModifier)) && e.source == PointerAxisSource::Wheel;
    if (!chord && !keyWheel && !titleBar)
        return false;
    const QPointF a = e.position - m_area.topLeft();
    const TRect c = m_camera.value;
    const krellis::Point world{c.x + c.w * a.x() / m_area.width(), c.y + c.h * a.y() / m_area.height()};
    // Wall clock, not m_nowMs: that only advances with paints, so it stands still between gestures.
    const double wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (m_nav.wheelStep(e.delta, world, wallMs))
        applyRest();
    return true;
}

// ------------------------------------------------------------------ rectangle select
QString KrellisCanvas::candidateLabel(const std::string &id) const
{
    const krellis::Entry *entry = m_nav.entries().find(id);
    if (!entry)
        return {};
    const auto leaves = krellis::leafIds(entry->node);
    if (leaves.size() == 1) {
        for (const auto &t : m_windows)
            if (t.panel == leaves.front())
                return t.window->caption();
    }
    return QStringLiteral("%1 windows").arg(leaves.size());
}

void KrellisCanvas::moveRectSelect(QPointF pointer)
{
    const krellis::Rect viewport{m_area.x(), m_area.y(), m_area.width(), m_area.height()};
    const TRect world = krellis::rectangleCamera({m_dragAnchor.x(), m_dragAnchor.y()}, {pointer.x(), pointer.y()}, viewport, m_rectView);
    const TRect s = toScreen(world); // against the live camera, which does not move during the gesture
    m_rectBand = QRectF(s.x, s.y, s.w, s.h);
    m_rectCandidate = (s.w >= 8 && s.h >= 8) ? krellis::bestFit(world, m_nav.entries()) : std::string();
    if (!m_rectCandidate.empty()) {
        if (const krellis::Entry *entry = m_nav.entries().find(m_rectCandidate)) {
            m_candTarget = panelScreen(entry->rect);
            m_candLabel = QStringLiteral("Release to focus %1 · Esc to cancel").arg(candidateLabel(m_rectCandidate));
        } else {
            m_rectCandidate.clear();
        }
    }
    updateMarquee(0);
    schedule();
}

void KrellisCanvas::finishRectSelect(bool commit)
{
    const std::string pick = commit && !m_rectCandidate.empty() ? m_rectCandidate : m_nav.framed();
    m_rectCandidate.clear();
    m_rectBand = QRectF();
    m_nav.focus(pick);
    applyRest();
    schedule();
}

void KrellisCanvas::updateMarquee(double dt)
{
    const bool live = m_drag == Drag::Rect;
    const bool hasCand = live && !m_rectCandidate.empty();
    const bool hinting = m_resize && !m_resize->floating && m_resize->stash;
    const double k = dt > 0 ? 1 - std::exp(-dt / 0.07) : 0;
    if (hinting) {
        // Where the drawer would sit: a tab-thick strip of the edge, as long as the tile is.
        const QRectF p(m_resize->painted.x, m_resize->painted.y, m_resize->painted.w, m_resize->painted.h);
        const QRectF a = m_area;
        const double t = m_drawerTabPx;
        switch (*m_resize->stash) {
        case krellis::Edge::Left: m_squeezeHint = QRectF(a.x(), p.y(), t, p.height()); break;
        case krellis::Edge::Right: m_squeezeHint = QRectF(a.right() - t, p.y(), t, p.height()); break;
        case krellis::Edge::Top: m_squeezeHint = QRectF(p.x(), a.y(), p.width(), t); break;
        case krellis::Edge::Bottom: m_squeezeHint = QRectF(p.x(), a.bottom() - t, p.width(), t); break;
        }
        m_squeezeHint = m_squeezeHint.intersected(a);
        m_squeezeEdge = int(*m_resize->stash);
    }
    m_squeezeOpacity += ((hinting ? 1.0 : 0.0) - m_squeezeOpacity) * (dt > 0 ? k : 1);
    m_bandOpacity += ((live && !m_rectBand.isEmpty() ? 1.0 : 0.0) - m_bandOpacity) * (dt > 0 ? k : 1);
    m_candOpacity += ((hasCand ? 1.0 : 0.0) - m_candOpacity) * (dt > 0 ? k : 0.5);
    if (hasCand) {
        // The candidate glides to its target rather than jumping between tiles.
        const double kc = dt > 0 ? 1 - std::exp(-dt / 0.05) : 0;
        if (m_candShown.w <= 0 || m_candOpacity < 0.05)
            m_candShown = m_candTarget;
        else
            m_candShown = krellis::lerpRect(m_candShown, m_candTarget, kc);
    }
    if (!m_marquee) {
        if (!live && !hinting)
            return;
        m_marquee = makeScene(QStringLiteral("marquee.qml"));
        if (!m_marquee || !m_marquee->rootItem()) {
            qCWarning(krellisLog) << "marquee.qml failed to load";
            m_marquee.reset();
            return;
        }
        m_marquee->setGeometry(Rect(m_area.toAlignedRect()));
    }
    if (!live && !hinting && m_bandOpacity < 0.01 && m_candOpacity < 0.01 && m_squeezeOpacity < 0.01) {
        const Rect g = m_marquee->geometry();
        m_marquee.reset();
        m_bandOpacity = m_candOpacity = m_squeezeOpacity = 0;
        effects->addRepaint(Region(g));
        return;
    }
    QQuickItem *item = m_marquee->rootItem();
    item->setProperty("mX", m_rectBand.x());
    item->setProperty("mY", m_rectBand.y());
    item->setProperty("mW", m_rectBand.width());
    item->setProperty("mH", m_rectBand.height());
    item->setProperty("mOpacity", m_bandOpacity);
    item->setProperty("cX", m_candShown.x);
    item->setProperty("cY", m_candShown.y);
    item->setProperty("cW", m_candShown.w);
    item->setProperty("cH", m_candShown.h);
    item->setProperty("cOpacity", m_candOpacity);
    item->setProperty("hX", m_squeezeHint.x() - m_area.x());
    item->setProperty("hY", m_squeezeHint.y() - m_area.y());
    item->setProperty("hW", m_squeezeHint.width());
    item->setProperty("hH", m_squeezeHint.height());
    item->setProperty("hEdge", m_squeezeEdge);
    item->setProperty("hOpacity", m_squeezeOpacity);
    if (hinting)
        item->setProperty("hLabel", QStringLiteral("Release to stash as a drawer"));
    if (hasCand)
        item->setProperty("cLabel", m_candLabel);
    effects->addRepaint(Region(m_marquee->geometry()));
}

void KrellisCanvas::pinch(double scale, QPointF at, QPointF delta)
{
    const double factor = m_pinchScale > 0 ? scale / m_pinchScale : 1.0;
    m_pinchScale = scale;
    const QPointF a = at - m_area.topLeft();
    // A pinch out (scale > 1) zooms in: the camera shrinks.
    m_nav.zoom(1.0 / std::max(0.01, factor), {a.x() / m_area.width(), a.y() / m_area.height()}, {-delta.x(), -delta.y()});
}

void KrellisCanvas::endPinch()
{
    m_pinchScale = 1;
    m_nav.finishGesture();
    applyRest();
}


// ------------------------------------------------------------------ drag and drop
double KrellisCanvas::inset() const { return GAP; }

krellis::Rect KrellisCanvas::toScreen(const krellis::Rect &w) const
{
    const TRect &c = m_camera.value;
    return {(w.x - c.x) / c.w * m_area.width(), (w.y - c.y) / c.h * m_area.height(), w.w / c.w * m_area.width(),
            w.h / c.h * m_area.height()};
}

krellis::Point KrellisCanvas::fromScreen(krellis::Point p) const
{
    const TRect &c = m_camera.value;
    return {c.x + p.x / m_area.width() * c.w, c.y + p.y / m_area.height() * c.h};
}

krellis::Rect KrellisCanvas::panelScreen(const krellis::Rect &world) const
{
    const TRect r = panelAt(world, m_camera.value);
    return {r.x - m_area.x(), r.y - m_area.y(), r.w, r.h};
}

std::optional<krellis::Rect> KrellisCanvas::fromScreenRect(const std::optional<krellis::Rect> &s) const
{
    if (!s)
        return std::nullopt;
    const TRect &c = m_camera.value;
    return TRect{c.x + (s->x - GAP) / m_area.width() * c.w, c.y + (s->y - GAP) / m_area.height() * c.h,
                 (s->w + 2 * GAP) / m_area.width() * c.w, (s->h + 2 * GAP) / m_area.height() * c.h};
}

const krellis::RectMap &KrellisCanvas::lastRects() const { return m_lastRects; }

// Callers hold iterators into lastRects(), so it only changes at frame boundaries.
void KrellisCanvas::refreshRects()
{
    m_lastRects.clear();
    const auto add = [&](const std::string &id) {
        if (const TRect *w = worldRect(id))
            m_lastRects[id] = panelScreen(m_tween.apply(id, *w));
    };
    if (m_session && m_session->active()) {
        for (const auto &kv : m_session->layoutTargets())
            add(kv.first);
    } else {
        for (const auto &id : leafIds(m_doc.root))
            add(id);
    }
    for (const auto &f : m_doc.floating)
        if (const auto r = floatScreen(f.panel->id))
            m_lastRects[f.panel->id] = {r->x - m_area.x(), r->y - m_area.y(), r->w, r->h};
}

void KrellisCanvas::beginTween(const krellis::RectMap &previous, double nowMs)
{
    std::map<std::string, TRect> from;
    for (const auto &kv : previous)
        if (const auto w = fromScreenRect(kv.second))
            from[kv.first] = *w;
    if (m_glide) m_tween.begin(from, nowMs);
}

void KrellisCanvas::userMoveStarted(EffectWindow *w)
{
    if (w->isUserResize()) { startResize(w); return; }
    if (!w->isUserMove() || m_session || !trackedFor(w))
        return;
    m_dragFromKWin = true;
    startDrag(w, w->window() ? w->window()->interactiveMoveResizeAnchor() : effects->cursorPos());
}

void KrellisCanvas::userMoveFinished(EffectWindow *w)
{
    if (m_resize && m_resize->window == w) {
        // KWin requests the initial geometry before emitting finish on Escape.
        const bool commit = m_resize->decision.value_or(w->window()
            && (w->window()->moveResizeGeometry() != m_resize->native || m_resize->lastNative == m_resize->native));
        finishResize(commit);
        QTimer::singleShot(0, this, [this] { applyRest(); });
        return;
    }
    if (!m_session || w != m_dragWindow || !m_dragFromKWin)
        return;
    // KWin restores the original geometry when the user cancels with Escape.
    const bool commit = w->frameGeometry() != m_dragStartGeometry;
    endDrag(commit);
    QTimer::singleShot(0, this, [this] { applyRest(); });
}

void KrellisCanvas::startDrag(EffectWindow *w, QPointF pointer, const std::string &viewId)
{
    m_windowPointer = pointer;
    const Tracked *t = trackedFor(w);
    if (!t) t = stowedFor(w); // a tab in hand is not the one on show
    if (!t || m_session)
        return;
    forgetDrawer(t->panel); // grabbed: it is a plain float from here on
    refreshRects();
    const QPointF rel = pointer - m_area.topLeft();
    m_session = std::make_unique<krellis::DragSession>(*this, t->panel, viewId, krellis::Point{rel.x(), rel.y()});
    m_dragWindow = w;
    m_dragStartGeometry = w->frameGeometry();
    schedule();
}

void KrellisCanvas::adoptDrag(EffectWindow *w, QPointF pointer, const QRectF &startGeometry)
{
    if (!trackedFor(w) || m_session)
        return;
    m_flights.erase(w);
    m_dragFromKWin = true;
    // The window just arrived as a tile at the end of this document. Grab it by the middle of its title
    // bar so the lifted card follows the real pointer instead of inheriting an offset from that slot.
    refreshRects();
    const TRect tile = screenRect(trackedFor(w)->panel);
    startDrag(w, QPointF(tile.x + tile.w / 2, tile.y + 14));
    m_dragStartGeometry = startGeometry; // Escape restores the original place, which must still read as a cancel
    moveDrag(pointer);
}

// Holding Shift while dragging floats the window where it is dropped, and keeps a float from docking.
bool KrellisCanvas::floatModifierHeld() const
{
    return m_simForceFloat >= 0 ? m_simForceFloat > 0 : m_shiftHeld;
}

void KrellisCanvas::moveDrag(QPointF pointer)
{
    m_windowPointer = pointer;
    if (!m_session)
        return;
    m_session->setForceFloat(floatModifierHeld());
    m_session->setFloatFirst(m_floatFirst);
    const QPointF rel = pointer - m_area.topLeft();
    m_session->move({rel.x(), rel.y()}, m_nowMs);
    applyRest();
    schedule();
}

void KrellisCanvas::endDrag(bool commit)
{
    if (!m_session)
        return;
    EffectWindow *moving = m_dragWindow;
    const DragOutcome out = m_session->end(commit, m_nowMs);
    m_session.reset(); // from here the document layout is what is shown
    m_dragWindow = nullptr;
    m_dragFromKWin = false;
    m_dragTabView.clear();
    if (!out.wasDrag) {
        schedule();
        return;
    }
    if (out.committed) {
        EditScope edit(*this, "move window");
        m_doc = out.next;
    }
    // Everything, including the lifted window, glides from where it was just drawn. The tween starts
    // before the relayout so the first size request is the drawn one, not the final one.
    std::map<std::string, TRect> from;
    for (const auto &kv : out.from)
        if (const auto w = fromScreenRect(kv.second))
            from[kv.first] = *w;
    if (m_glide) m_tween.begin(from, m_nowMs);
    relayout(false);
    refreshRects();
    // A window that ends up floating glides from the card it was drawn as.
    if (const auto lifted = out.from.find(out.liftedId); lifted != out.from.end() && floatScreen(out.liftedId))
        for (const auto &t : m_windows)
            if (t.panel == out.liftedId)
                startFlight(t.window, FlightKind::Glide, {lifted->second.x + m_area.x(), lifted->second.y + m_area.y(), lifted->second.w, lifted->second.h}, {});
    schedule();
    if (commit && moving && m_transfer && m_output && !QRectF(QRect(m_output->geometry())).contains(m_windowPointer))
        m_transfer(moving, m_windowPointer);
}

// A new or docking window takes an empty slot (under the cursor when given, else the first) before it splits a tile.
bool KrellisCanvas::fillVacancyFor(const krellis::NodePtr &panel, const std::optional<QPointF> &cursor)
{
    const auto empty = vacancies(m_doc.root);
    if (empty.empty())
        return false;
    std::string pick = empty.front();
    if (cursor && m_area.contains(*cursor)) {
        const QPointF rel = *cursor - m_area.topLeft();
        const krellis::Point point = fromScreen({rel.x(), rel.y()});
        for (const auto &id : empty)
            if (const TRect *r = worldRect(id); r && point.x >= r->x && point.x <= r->x + r->w && point.y >= r->y && point.y <= r->y + r->h) {
                pick = id;
                break;
            }
    }
    m_doc = fillVacancy(m_doc, pick, panel);
    return true;
}

// The drop slot: a translucent placeholder where the dragged window will land.
void KrellisCanvas::updateSlot(double dt)
{
    const bool active = m_session && m_session->active();
    const TRect *world = active ? worldRect(krellis::DROP_SLOT) : nullptr;
    // A float that would snap to an edge or corner shows where it will land.
    const std::optional<TRect> snap = active && !world ? m_session->snapZone() : std::nullopt;
    double want = 0;
    if (world || snap) {
        const TRect r = world ? panelAt(m_tween.apply(krellis::DROP_SLOT, *world), m_camera.value)
                              : TRect{snap->x + m_area.x(), snap->y + m_area.y(), snap->w, snap->h};
        // A slot that is still collapsing out of a seam is barely there; it fades in as it opens.
        const double size = std::min(r.w, r.h);
        want = std::clamp(size / 40.0, 0.0, 1.0);
        if (r.w >= 1 && r.h >= 1) {
            if (!m_slot) {
                m_slot = makeScene(QStringLiteral("slot.qml"));
                if (!m_slot || !m_slot->rootItem()) {
                    qCWarning(krellisLog) << "slot.qml failed to load";
                    m_slot.reset();
                    return;
                }
                m_slot->setGeometry(Rect(m_area.toAlignedRect()));
            }
            QQuickItem *item = m_slot->rootItem();
            item->setProperty("slotX", r.x - m_area.x());
            item->setProperty("slotY", r.y - m_area.y());
            item->setProperty("slotW", r.w);
            item->setProperty("slotH", r.h);
        }
    }
    if (!m_slot)
        return;
    // Ease the opacity (about a 70 ms time constant).
    m_slotOpacity += (want - m_slotOpacity) * (1 - std::exp(-dt / 0.07));
    if (!world && !snap && m_slotOpacity < 0.01) {
        const Rect g = m_slot->geometry();
        m_slot.reset();
        m_slotOpacity = 0;
        effects->addRepaint(Region(g));
        return;
    }
    m_slot->rootItem()->setProperty("slotOpacity", m_slotOpacity);
    effects->addRepaint(Region(m_slot->geometry()));
}

double KrellisCanvas::realMs()
{
    using namespace std::chrono;
    return double(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void KrellisCanvas::commitEdit(const char *label, const krellis::LayoutDocument &before, const krellis::LayoutDocument &next, const std::string &key)
{
    if (krellis::sameLayout(before, next)) return;
    m_history.record(before, label, key, std::int64_t(realMs()));
}

bool KrellisCanvas::undoRedo(bool redo)
{
    if (m_session || m_resize) {
        toast(QStringLiteral("Finish the drag first"));
        return false;
    }
    const auto entry = redo ? m_history.redo(m_doc) : m_history.undo(m_doc);
    if (!entry) {
        toast(redo ? QStringLiteral("Nothing to redo") : QStringLiteral("Nothing to undo"));
        return false;
    }
    std::set<std::string> fromNow; // scratch windows belong to the scratchpad, not to the edit history
    for (const auto &t : m_windows)
        if (scratchWindow(t.window)) fromNow.insert(t.panel);
    m_doc = krellis::reconcile(entry->doc, m_doc, fromNow);
    relayout(true);
    updateDock();
    applyRest();
    toast(QStringLiteral("%1: %2").arg(redo ? QStringLiteral("Redid") : QStringLiteral("Undid"), QString::fromStdString(entry->label)));
    return true;
}

void KrellisCanvas::toast(const QString &text)
{
    if (!m_toasts || m_unloading) return;
    ++m_toastGen;
    m_toastHoldArmed = m_toastHoldDone = false;
    if (text == m_toastText && m_toast) {
        m_toastPending = true;
        m_toastResume = true; // already up: the timer restarts without sliding in again
    } else {
        m_toastResume = m_toast != nullptr;
        m_toastText = text;
        m_toastPending = true;
    }
    schedule();
}

// One message at a time. It slides up and fades in, holds, then fades out and the scene goes away so an idle
// desktop stays idle. A message that replaces a visible one skips the slide and just swaps the text.
void KrellisCanvas::showSheet(const QVariantList &groups)
{
    if (m_unloading) return;
    m_sheetGroups = groups;
    m_sheetWanted = true;
    schedule();
}

void KrellisCanvas::hideSheet()
{
    if (!m_sheetWanted && !m_sheet) return;
    m_sheetWanted = false;
    schedule();
}

void KrellisCanvas::updateSheet(double dt)
{
    constexpr double IN_MS = 160, OUT_MS = 120; // a fade only: nothing moves, so reduced motion needs no variant
    m_sheetBusy = false;
    if (!m_sheetWanted && !m_sheet) return;
    if (!m_sheet) {
        m_sheet = makeScene(QStringLiteral("whichkey.qml"));
        if (!m_sheet || !m_sheet->rootItem()) {
            qCWarning(krellisLog) << "whichkey.qml failed to load";
            m_sheet.reset();
            m_sheetWanted = false;
            return;
        }
        m_sheet->setGeometry(Rect(QRect(int(m_area.left()), int(m_area.top()), int(m_area.width()), int(m_area.height()))));
    }
    const double step = dt * 1000 / (m_sheetWanted ? IN_MS : OUT_MS);
    m_sheetOpacity = std::clamp(m_sheetOpacity + (m_sheetWanted ? step : -step), 0.0, 1.0);
    if (!m_sheetWanted && m_sheetOpacity <= 0) {
        const Rect g = m_sheet->geometry();
        m_sheet.reset();
        effects->addRepaint(Region(g));
        return;
    }
    QQuickItem *item = m_sheet->rootItem();
    item->setProperty("groups", m_sheetGroups);
    item->setProperty("hintOpacity", m_sheetOpacity);
    effects->addRepaint(Region(m_sheet->geometry()));
    m_sheetBusy = !m_sheetWanted || m_sheetOpacity < 1;
}

void KrellisCanvas::updateToast()
{
    constexpr double IN = 180, HOLD = 1400, OUT = 220, RISE = 12;
    m_toastBusy = false;
    if (m_toastText.isEmpty()) {
        if (m_toast) {
            const Rect g = m_toast->geometry();
            m_toast.reset();
            effects->addRepaint(Region(g));
        }
        return;
    }
    if (!m_toast) {
        m_toast = makeScene(QStringLiteral("toast.qml"));
        if (!m_toast || !m_toast->rootItem()) {
            qCWarning(krellisLog) << "toast.qml failed to load";
            m_toast.reset();
            m_toastText.clear();
            return;
        }
        const QRect band(m_area.left(), int(m_area.bottom()) - 96, int(m_area.width()), 96);
        m_toast->setGeometry(Rect(band));
    }
    if (m_toastPending) {
        m_toastPending = false;
        m_toastStartMs = m_nowMs - (m_toastResume ? IN : 0);
    }
    if (m_toastHoldDone) { // the hold timer fired: start fading out from here
        m_toastHoldDone = false;
        m_toastStartMs = m_nowMs - (IN + HOLD) - 1; // just past the hold, so this frame already counts as fading
    }
    const double age = m_nowMs - m_toastStartMs;
    if (age >= IN + HOLD + OUT) {
        m_toastText.clear();
        updateToast();
        return;
    }
    const double in = std::clamp(age / IN, 0.0, 1.0);
    const double out = age > IN + HOLD ? std::clamp((age - IN - HOLD) / OUT, 0.0, 1.0) : 0;
    QQuickItem *item = m_toast->rootItem();
    item->setProperty("text", m_toastText);
    item->setProperty("toastOpacity", krellis::easeOutQuint(in) * (1 - out));
    item->setProperty("rise", reducedMotion() ? 0.0 : RISE * (1 - krellis::easeOutQuint(in)));
    effects->addRepaint(Region(m_toast->geometry()));
    if (age >= IN && age <= IN + HOLD) {
        if (!m_toastHoldArmed) {
            m_toastHoldArmed = true;
            const int gen = m_toastGen;
            QTimer::singleShot(std::chrono::milliseconds(int(IN + HOLD - age)), Qt::PreciseTimer, this, [this, gen] {
                if (gen != m_toastGen || m_unloading) return;
                m_toastHoldDone = true;
                schedule();
            });
        }
    } else {
        m_toastBusy = true; // sliding in or fading out
    }
}

bool KrellisCanvas::drawerEdgeFree(krellis::Edge edge) const
{
    if (!m_output) return true;
    const QRectF mine(m_output->geometry());
    for (const auto &other : effects->screens()) {
        if (other == m_output) continue;
        const QRectF g(other->geometry());
        const bool overlapX = std::min(g.right(), mine.right()) - std::max(g.left(), mine.left()) > 0;
        const bool overlapY = std::min(g.bottom(), mine.bottom()) - std::max(g.top(), mine.top()) > 0;
        switch (edge) {
        case krellis::Edge::Left: if (overlapY && std::abs(g.right() - mine.left()) <= 1) return false; break;
        case krellis::Edge::Right: if (overlapY && std::abs(g.left() - mine.right()) <= 1) return false; break;
        case krellis::Edge::Top: if (overlapX && std::abs(g.bottom() - mine.top()) <= 1) return false; break;
        case krellis::Edge::Bottom: if (overlapX && std::abs(g.top() - mine.bottom()) <= 1) return false; break;
        }
    }
    return true;
}

void KrellisCanvas::stashPanel(const std::string &panel, krellis::Edge edge, const TRect &from)
{
    if (!drawerEdgeFree(edge)) return;
    const auto loc = krellis::locatePanel(m_doc, panel);
    if (loc.where != krellis::PanelLocation::Where::Docked || leafIds(m_doc.root).size() < 2) return;
    // Revealed, it keeps the tile's own size and hugs the edge it slides out of; the neighbours reflow into the tile's place.
    TRect open = from;
    switch (edge) {
    case krellis::Edge::Left: open.x = m_area.x(); break;
    case krellis::Edge::Right: open.x = m_area.right() - open.w; break;
    case krellis::Edge::Top: open.y = m_area.y(); break;
    case krellis::Edge::Bottom: open.y = m_area.bottom() - open.h; break;
    }
    const krellis::Rect rect{(open.x - m_area.x()) / m_area.width(), (open.y - m_area.y()) / m_area.height(),
                             open.w / m_area.width(), open.h / m_area.height()};
    {
        EditScope edit(*this, "stash as drawer");
        m_doc = krellis::floatPanel(m_doc, loc.panel, rect, krellis::FloatingLayer::Overlay);
    }
    m_drawers[panel] = {edge, false, ++m_drawerSeq};
}

void KrellisCanvas::flipActive()
{
    if (m_resize) finishResize(false);
    std::vector<std::string> panels;
    for (const auto &t : m_windows) panels.push_back(t.panel);
    const std::string panel = dockedPanelOf(*this, m_doc, activeIndex(), panels);
    if (panel.empty() || m_session) return;
    auto next = krellis::flipPanel(m_doc, panel);
    if (!next) return;
    EditScope edit(*this, "flip split");
    m_doc = std::move(*next);
    relayout(true);
    m_nav.reveal(panel);
    applyRest();
}

// The gap between two siblings, as the split that holds them and the index of the first. The gap is
// 2 * GAP wide; a couple of pixels more are allowed so a click that just misses it still counts.
bool KrellisCanvas::seamAt(QPointF pos, std::string *split, int *index) const
{
    if (m_session || m_resize || !m_doc.root) return false;
    const TRect &cam = m_camera.value;
    const double reach = GAP + 2;
    std::function<bool(const krellis::NodePtr &)> walk = [&](const krellis::NodePtr &node) {
        if (!node) return false;
        if (node->kind == krellis::Node::Kind::Stage) return walk(node->child);
        if (node->kind != krellis::Node::Kind::Split) return false;
        for (size_t i = 0; i + 1 < node->children.size(); ++i) {
            const Entry *a = m_rects.find(node->children[i]->id);
            if (!a) continue;
            const TRect r = panelAt(a->rect, cam);
            const bool x = node->axis == krellis::Axis::X;
            const double seam = x ? r.x + r.w + GAP : r.y + r.h + GAP;
            const double along = x ? pos.x() : pos.y(), across = x ? pos.y() : pos.x();
            const double lo = (x ? r.y : r.x) - GAP, hi = lo + (x ? r.h : r.w) + 2 * GAP;
            if (std::abs(along - seam) <= reach && across >= lo && across <= hi) {
                *split = node->id;
                *index = int(i);
                return true;
            }
        }
        for (const auto &c : node->children)
            if (walk(c)) return true;
        return false;
    };
    return walk(m_doc.root);
}

void KrellisCanvas::stashActive()
{
    if (m_resize) finishResize(false);
    const int i = activeIndex();
    if (i < 0) return;
    const Tracked &t = m_windows[size_t(i)];
    if (m_drawers.count(t.panel)) { // a second press puts it back among the tiles
        forgetDrawer(t.panel);
        toggleFloat(i);
        return;
    }
    const auto loc = krellis::locatePanel(m_doc, t.panel);
    double opacity = 1;
    const TRect from = visualRect(t.window, &opacity).value_or(screenRect(t.panel));
    // Always a side one: the top and bottom edges are where the panels sit. Drawers on a side stack.
    const double cx = from.x + from.w / 2 - m_area.x();
    // A side that joins another display is out (the pointer crossing over would open the drawer): take the other one.
    krellis::Edge edge = cx <= m_area.width() - cx ? krellis::Edge::Left : krellis::Edge::Right;
    if (!drawerEdgeFree(edge)) edge = edge == krellis::Edge::Left ? krellis::Edge::Right : krellis::Edge::Left;
    if (!drawerEdgeFree(edge)) return;
    if (loc.where == krellis::PanelLocation::Where::Docked) {
        if (!rulesFor(t.window).floating) return;
        stashPanel(t.panel, edge, from);
        if (!m_drawers.count(t.panel)) return; // the only tile stays
    } else if (loc.where == krellis::PanelLocation::Where::Floating) {
        for (auto &f : m_doc.floating) {
            if (f.panel->id != t.panel) continue;
            if (f.layer != krellis::FloatingLayer::Overlay) return;
            switch (edge) {
            case krellis::Edge::Left: f.rect.x = 0; break;
            case krellis::Edge::Right: f.rect.x = 1 - f.rect.w; break;
            case krellis::Edge::Top: f.rect.y = 0; break;
            case krellis::Edge::Bottom: f.rect.y = 1 - f.rect.h; break;
            }
        }
        m_drawers[t.panel] = {edge, false, ++m_drawerSeq};
    } else {
        return;
    }
    // Keyboard focus moves on to a window that is still in view.
    for (const auto &other : m_windows)
        if (other.window != t.window && !m_drawers.count(other.panel) && krellis::locatePanel(m_doc, other.panel).where == krellis::PanelLocation::Where::Docked) {
            effects->activateWindow(other.window);
            break;
        }
    relayout(true);
    startFlight(t.window, FlightKind::Glide, from, {}, opacity);
}

void KrellisCanvas::forgetDrawer(const std::string &panel)
{
    if (!m_drawers.erase(panel)) return;
    for (const auto &t : m_windows)
        if (t.panel == panel) effects->setElevatedWindow(t.window, false);
}

void KrellisCanvas::setDrawerOpen(const std::string &panel, bool open, bool pinned)
{
    const auto d = m_drawers.find(panel);
    if (d == m_drawers.end() || d->second.open == open) return;
    for (const auto &t : m_windows) {
        if (t.panel != panel) continue;
        double opacity = 1;
        const TRect from = visualRect(t.window, &opacity).value_or(floatScreen(panel).value_or(screenRect(panel)));
        d->second.open = open;
        d->second.pinned = open && pinned;
        effects->setElevatedWindow(t.window, open); // paint above the other slivers
        if (open) m_doc = krellis::raiseFloat(m_doc, panel);
        relayout(true);
        startFlight(t.window, FlightKind::Glide, from, {}, opacity);
        return;
    }
}

void KrellisCanvas::slideOutDrawer(const std::string &panel)
{
    const auto d = m_drawers.find(panel);
    if (d == m_drawers.end() || d->second.open) return;
    for (const auto &[other, o] : std::map<std::string, Drawer>(m_drawers))
        if (o.open) setDrawerOpen(other, false);
    setDrawerOpen(panel, true, true);
}

void KrellisCanvas::updateDrawerHover(const PointerMotionEvent &event)
{
    if (m_drawers.empty() || m_session || m_resize || event.buttons != Qt::NoButton) return;
    for (auto &[panel, d] : m_drawers) {
        const auto r = floatScreen(panel);
        if (!r) continue;
        QRectF zone(r->x, r->y, r->w, r->h);
        if (d.open) zone.adjust(-8, -8, 8, 8);
        else if (const auto slice = drawerSlice(panel)) {
            zone = QRectF(slice->x, slice->y, slice->w, slice->h);
            if (const auto tab = drawerTab(panel)) zone = zone.united(QRectF(tab->x, tab->y, tab->w, tab->h));
        }
        const bool want = zone.contains(event.position);
        if (d.pinned) { // opened from the minimap: it stays until the pointer has been over it and left
            if (want) d.pinned = false;
            continue;
        }
        if (want == d.open) continue;
        // Only one slides out at a time: one still open keeps the pointer until it leaves.
        if (want && std::any_of(m_drawers.begin(), m_drawers.end(), [](const auto &o) { return o.second.open; })) continue;
        setDrawerOpen(panel, want);
        return; // relayout may have invalidated the map iteration
    }
}

void KrellisCanvas::toggleFloat(int i)
{
    if (m_resize) finishResize(false);
    if (i < 0 || i >= int(m_windows.size()))
        return;
    const Tracked &t = m_windows[size_t(i)];
    const auto loc = krellis::locatePanel(m_doc, t.panel);
    double opacity = 1;
    const TRect from = visualRect(t.window, &opacity).value_or(screenRect(t.panel));
    EditScope edit(*this, loc.where == krellis::PanelLocation::Where::Floating ? "tile window" : "float window");
    if (loc.where == krellis::PanelLocation::Where::Floating) {
        if (!rulesFor(t.window).side) return;
        m_doc = krellis::removePanel(m_doc, t.panel);
        dockAtEnd(loc.panel);
    } else if (loc.where == krellis::PanelLocation::Where::Docked) {
        if (!rulesFor(t.window).floating) return;
        // Float-first leaves the slot empty for the next window; tiling reflows the neighbours into it
        // and keeps at least one tiled panel so the canvas never empties.
        const auto vacated = m_floatFirst ? krellis::vacatePanel(m_doc, loc.panel->id) : std::nullopt;
        if (!vacated && leafIds(m_doc.root).size() < 2)
            return;
        // Pop out slightly smaller than the tile, centred on it.
        const TRect a = from;
        const double w = a.w * 0.8, h = a.h * 0.8;
        const krellis::Rect rect{(a.x + (a.w - w) / 2 - m_area.x()) / m_area.width(), (a.y + (a.h - h) / 2 - m_area.y()) / m_area.height(),
                                 w / m_area.width(), h / m_area.height()};
        if (vacated)
            m_doc = *vacated;
        m_doc = krellis::floatPanel(m_doc, loc.panel, rect, krellis::FloatingLayer::Overlay);
    } else {
        return;
    }
    relayout(true);
    startFlight(t.window, FlightKind::Glide, from, {}, opacity);
}

bool KrellisCanvas::simDragStart(int i, double x, double y)
{
    if (i < 0 || i >= int(m_windows.size()))
        return false;
    startDrag(m_windows[size_t(i)].window, {x, y});
    return m_session != nullptr;
}
void KrellisCanvas::simDragMove(double x, double y) { moveDrag({x, y}); }
void KrellisCanvas::simDragEnd(bool commit) { endDrag(commit); }

void KrellisCanvas::beginFramingName()
{
    m_naming = true;
    m_nameError = false;
    updateBar();
    if (m_bar && m_bar->rootItem()) QMetaObject::invokeMethod(m_bar->rootItem(), "beginName");
}
void KrellisCanvas::submitFramingName()
{
    if (!m_bar || !m_bar->rootItem()) return;
    const QString name = m_bar->rootItem()->property("framingName").toString().trimmed();
    if (name.isEmpty()) { m_nameError = true; updateBar(); return; }
    if (m_nav.saveFraming(name.toStdString())) {
        m_naming = false;
        m_nameError = false;
    }
    m_barSignature.clear();
    updateBar();
    schedule();
}

// ------------------------------------------------------------------ D-Bus control
QString KrellisCanvas::dump()
{
    QStringList lines;
    lines << QStringLiteral("camera %1 %2 %3 %4 (target %5 %6 %7 %8)")
                 .arg(m_camera.value.x).arg(m_camera.value.y).arg(m_camera.value.w).arg(m_camera.value.h)
                 .arg(m_camera.target.x).arg(m_camera.target.y).arg(m_camera.target.w).arg(m_camera.target.h);
    lines << QStringLiteral("framed '%1'").arg(QString::fromStdString(m_nav.framed()));
    for (const auto &t : m_windows) {
        const TRect s = screenRect(t.panel);
        const RectF f = t.window->frameGeometry();
        lines << QStringLiteral("%1 '%2' screen %3,%4 %5x%6 frame %7,%8 %9x%10")
                     .arg(QString::fromStdString(t.panel), t.window->caption())
                     .arg(s.x, 0, 'f', 1).arg(s.y, 0, 'f', 1).arg(s.w, 0, 'f', 1).arg(s.h, 0, 'f', 1)
                     .arg(f.x(), 0, 'f', 1).arg(f.y(), 0, 'f', 1).arg(f.width(), 0, 'f', 1).arg(f.height(), 0, 'f', 1);
        if (const auto *window = t.window->window()) {
            const auto body = window->clientGeometry();
            lines.back() += QStringLiteral(" client %1,%2 %3x%4").arg(body.x()).arg(body.y()).arg(body.width()).arg(body.height());
        }
        lines.back() += QStringLiteral(" app '%1'").arg(appId(t.window));
        double drawnOpacity = 1;
        if (const auto drawn = visualRect(t.window, &drawnOpacity))
            lines.back() += QStringLiteral(" drawn %1x%2").arg(drawn->w, 0, 'f', 1).arg(drawn->h, 0, 'f', 1)
                + QStringLiteral(" drawnpos %1,%2").arg(drawn->x, 0, 'f', 1).arg(drawn->y, 0, 'f', 1)
                + QStringLiteral(" opacity %1").arg(drawnOpacity, 0, 'f', 2);
        const auto rule = rulesFor(t.window);
        if (rule.scaling == krellis::ViewScaling::Inert) lines.back() += QStringLiteral(" INERT");
        if (rule.scaling == krellis::ViewScaling::None) lines.back() += QStringLiteral(" NO_SCALE");
        if (!rule.closable) lines.back() += QStringLiteral(" NO_CLOSE");
        if (frameOnly(t.panel)) lines.back() += QStringLiteral(" ICON");
        if (floatScreen(t.panel))
            lines.back() += QStringLiteral(" FLOAT");
        if (isPinned(t.panel))
            lines.back() += pinRect(t.panel) ? QStringLiteral(" PIN-SHOWN") : QStringLiteral(" PIN");
        TRect fr;
        double op = 1;
        if (flightRect(t.window, t, fr, op))
            lines.back() += QStringLiteral(" FLY %1,%2 %3x%4 opacity %5").arg(fr.x, 0, 'f', 1).arg(fr.y, 0, 'f', 1).arg(fr.w, 0, 'f', 1).arg(fr.h, 0, 'f', 1).arg(op, 0, 'f', 2);
        if (const auto flight = m_flights.find(t.window); flight != m_flights.end()) {
            const auto &f = flight->second;
            lines.back() += QStringLiteral(" from %1,%2 %3x%4 baseOpacity %5")
                .arg(f.from.x).arg(f.from.y).arg(f.from.w).arg(f.from.h).arg(f.fromOpacity);
        }
    }
    if (m_scratchState) {
        const auto s = m_scratchState();
        lines << QStringLiteral("SCRATCH count %1 shown %2 active %3 enabled %4").arg(s.value("count").toInt()).arg(s.value("shown").toBool()).arg(s.value("active").toBool()).arg(s.value("enabled").toBool());
        lines << QStringLiteral("LAYER dim %1").arg(m_layerDim, 0, 'f', 2);
    }
    for (const auto &t : m_stowed) {
        const RectF f = t.window->frameGeometry();
        lines << QStringLiteral("TAB %1 %2 '%3' stowed frame %4,%5 %6x%7").arg(QString::fromStdString(t.panel), QString::fromStdString(t.view), t.window->caption())
                     .arg(f.x(), 0, 'f', 1).arg(f.y(), 0, 'f', 1).arg(f.width(), 0, 'f', 1).arg(f.height(), 0, 'f', 1);
    }
    for (const auto &st : m_strips) {
        QStringList titles;
        for (const auto &title : st.titles) titles << QString::fromStdString(title);
        lines << QStringLiteral("STRIP %1 %2,%3 %4x%5 tab %6 shown '%7' tabs [%8]").arg(QString::fromStdString(st.panel))
                     .arg(st.rect.x, 0, 'f', 1).arg(st.rect.y, 0, 'f', 1).arg(st.rect.w, 0, 'f', 1).arg(st.rect.h, 0, 'f', 1).arg(st.tabW, 0, 'f', 1)
                     .arg(QString::fromStdString(st.selected), titles.join(QStringLiteral(" | ")));
    }
    lines << QStringLiteral("HISTORY undo %1 redo %2").arg(m_history.size()).arg(m_history.redoSize());
    if (m_sheetWanted || m_sheet)
        lines << QStringLiteral("WHICHKEY %1 opacity %2").arg(m_sheetWanted ? "shown" : "hiding").arg(m_sheetOpacity, 0, 'f', 2);
    if (m_paletteOn || m_palette) {
        lines << QStringLiteral("PALETTE %1 query '%2' selected %3 of %4 opacity %5")
                     .arg(m_paletteOn ? "shown" : "hiding", m_paletteQuery).arg(m_paletteRanked.empty() ? -1 : m_paletteSel).arg(m_paletteRanked.size()).arg(m_paletteOpacity, 0, 'f', 2);
        const int end = std::min<int>(int(m_paletteRanked.size()), m_paletteFirst + PALETTE_ROWS);
        for (int rank = m_paletteFirst; rank < end; ++rank) {
            const auto &it = m_paletteItems[m_paletteRanked[size_t(rank)]];
            lines << QStringLiteral("PROW %1%2 %3 | %4 | %5%6").arg(rank == m_paletteSel ? ">" : " ").arg(rank).arg(it.title, it.app, it.detail, it.view ? " | view" : "");
        }
    }
    if (!m_toastText.isEmpty()) {
        lines << QStringLiteral("TOAST %1 age %2 opacity %3").arg(m_toastText).arg(m_toastPending ? 0.0 : m_nowMs - m_toastStartMs, 0, 'f', 0)
                     .arg(m_toast ? m_toast->rootItem()->property("toastOpacity").toDouble() : 0.0, 0, 'f', 2);
    }
    for (const auto &t : m_windows)
        if (scratchWindow(t.window)) lines << QStringLiteral("SCRATCHWIN %1 %2").arg(QString::fromStdString(t.panel), t.window->isMinimized() ? QStringLiteral("hidden") : QStringLiteral("shown"));
    {
        QStringList stack;
        for (EffectWindow *w : effects->stackingOrder())
            if (const auto *t = trackedFor(w)) stack << QString::fromStdString(t->panel);
        lines << QStringLiteral("STACK %1").arg(stack.join(QLatin1Char(' '))); // bottom to top
    }
    if (const int active = activeIndex(); active >= 0)
        lines << QStringLiteral("ACTIVE %1").arg(QString::fromStdString(m_windows[size_t(active)].panel));
    if (m_resize) lines << QStringLiteral("RESIZE %1 floating %2 boundaries %3 nativeSteps %4").arg(m_resize->window ? m_resize->window->caption() : QString()).arg(m_resize->floating).arg(m_resize->boundaries.size()).arg(m_resize->nativeSteps);
    if (m_session && m_session->active()) {
        const auto &tg = m_session->target();
        const auto &cd = m_session->candidate();
        lines << QStringLiteral("DRAG lifted %1,%2 %3x%4 target %5 candidate %6 collapsed %7")
                     .arg(m_lifted.x, 0, 'f', 1).arg(m_lifted.y, 0, 'f', 1).arg(m_lifted.w, 0, 'f', 1).arg(m_lifted.h, 0, 'f', 1)
                     .arg(tg ? int(tg->kind) : -1).arg(cd ? int(cd->kind) : -1).arg(m_session->collapsed());
        if (const auto snap = m_session->snapZone())
            lines << QStringLiteral("SNAP %1,%2 %3x%4").arg(snap->x).arg(snap->y).arg(snap->w).arg(snap->h);
        if (const TRect *sw = worldRect(krellis::DROP_SLOT)) {
            const TRect r = panelAt(m_tween.apply(krellis::DROP_SLOT, *sw), m_camera.value);
            lines << QStringLiteral("SLOT %1,%2 %3x%4 opacity %5").arg(r.x, 0, 'f', 1).arg(r.y, 0, 'f', 1).arg(r.w, 0, 'f', 1).arg(r.h, 0, 'f', 1).arg(m_slotOpacity, 0, 'f', 2);
        }
    }
    for (const auto &[panel, d] : m_drawers)
        if (const auto r = floatScreen(panel))
            lines << QStringLiteral("DRAWER %1 edge %2 %3 at %4,%5 %6x%7").arg(QString::fromStdString(panel)).arg(int(d.edge)).arg(d.open ? "open" : "closed")
                         .arg(r->x, 0, 'f', 1).arg(r->y, 0, 'f', 1).arg(r->w, 0, 'f', 1).arg(r->h, 0, 'f', 1);
    for (const auto &[panel, d] : m_drawers)
        if (const auto tab = drawerTab(panel))
            lines << QStringLiteral("TAB %1 at %2,%3 %4x%5 shown %6").arg(QString::fromStdString(panel)).arg(tab->x, 0, 'f', 1).arg(tab->y, 0, 'f', 1).arg(tab->w, 0, 'f', 1).arg(tab->h, 0, 'f', 1).arg(m_drawerTabs != nullptr);
    for (const auto &id : vacancies(m_doc.root))
        if (const TRect *vw = worldRect(id)) {
            const TRect r = panelAt(m_tween.apply(id, *vw), m_camera.value);
            lines << QStringLiteral("VACANT %1 %2,%3 %4x%5").arg(QString::fromStdString(id)).arg(r.x, 0, 'f', 1).arg(r.y, 0, 'f', 1).arg(r.w, 0, 'f', 1).arg(r.h, 0, 'f', 1);
        }
    {
        QStringList order;
        for (const auto &id : leafIds(m_doc.root))
            if (!isVacant(findNode(m_doc.root, id)))
                order << QString::fromStdString(id);
        for (const auto &[cw, c] : m_closing) {
            const double p = std::min(1.0, (m_nowMs - c.start) / CLOSE_MS);
            lines << QStringLiteral("CLOSING %1 progress %2 opacity %3").arg(cw->caption()).arg(p, 0, 'f', 2).arg(c.opacity * APPEAR_EASE(1 - p), 0, 'f', 2)
                + QStringLiteral(" from %1,%2 %3x%4 baseOpacity %5").arg(c.rect.x).arg(c.rect.y).arg(c.rect.w).arg(c.rect.h).arg(c.opacity);
        }
        if (m_drag == Drag::Rect)
            lines << QStringLiteral("RECT band %1,%2 %3x%4 candidate '%5' label '%6' opacity band %7 cand %8")
                         .arg(m_rectBand.x(), 0, 'f', 1).arg(m_rectBand.y(), 0, 'f', 1).arg(m_rectBand.width(), 0, 'f', 1).arg(m_rectBand.height(), 0, 'f', 1)
                         .arg(QString::fromStdString(m_rectCandidate), m_candLabel).arg(m_bandOpacity, 0, 'f', 2).arg(m_candOpacity, 0, 'f', 2);
        if (m_map)
            lines << QStringLiteral("MAP geometry %1,%2 %3x%4 shown %5 settled %6 mode %7 help %8 view %9x%10 root %11x%12 open %13 pinned %14")
                         .arg(m_map->geometry().x()).arg(m_map->geometry().y()).arg(m_map->geometry().width()).arg(m_map->geometry().height())
                         .arg(m_mapOn).arg(m_map->rootItem()->property("settled").toBool())
                         .arg(m_map->rootItem()->property("floatFirst").toBool() ? QStringLiteral("Floating") : QStringLiteral("Tiling"))
                         .arg(m_map->rootItem()->property("helpOpen").toBool())
                         .arg(m_map->size().width()).arg(m_map->size().height())
                         .arg(m_map->rootItem()->width()).arg(m_map->rootItem()->height())
                         .arg(m_map->rootItem()->property("slide").toDouble(), 0, 'f', 2).arg(m_mapPinned);
        if (m_map)
            for (const auto &f : m_doc.floating) {
                if (!m_drawers.count(f.panel->id)) continue;
                // Where the drawer's tile sits on screen in the minimap, for clicking it.
                const TRect t = drawerMapRect(f.panel->id, f.rect);
                const QRectF stage = m_map->rootItem()->property("stageRect").toRectF().translated(m_map->geometry().x(), m_map->geometry().y());
                lines << QStringLiteral("MAPTILE %1 at %2,%3 %4x%5").arg(QString::fromStdString(f.panel->id)).arg(stage.x() + t.x * stage.width(), 0, 'f', 1)
                             .arg(stage.y() + t.y * stage.height(), 0, 'f', 1).arg(t.w * stage.width(), 0, 'f', 1).arg(t.h * stage.height(), 0, 'f', 1);
            }
        if (m_squeezeOpacity > 0.01)
            lines << QStringLiteral("SQUEEZE edge %1 at %2,%3 %4x%5 opacity %6").arg(m_squeezeEdge).arg(m_squeezeHint.x(), 0, 'f', 1).arg(m_squeezeHint.y(), 0, 'f', 1)
                         .arg(m_squeezeHint.width(), 0, 'f', 1).arg(m_squeezeHint.height(), 0, 'f', 1).arg(m_squeezeOpacity, 0, 'f', 2);
        lines << QStringLiteral("ORDER ") + order.join(QLatin1Char(' '));
    }
    for (EffectWindow *w : effects->stackingOrder()) {
        if (!w->isPopupWindow() || w->isDeleted()) continue;
        if (const auto shown = visualRect(w)) {
            const auto f = w->frameGeometry();
            lines << QStringLiteral("POPUP '%1' screen %2,%3 %4x%5 frame %6,%7 %8x%9")
                .arg(w->caption()).arg(shown->x).arg(shown->y).arg(shown->w).arg(shown->h)
                .arg(f.x()).arg(f.y()).arg(f.width()).arg(f.height());
        }
    }
    lines << QStringLiteral("RULES %1 file %2").arg(m_rules.size()).arg(m_rulesFile);
    lines << QStringLiteral("NAMING %1 error %2").arg(m_naming).arg(m_nameError);
    for (const auto &framing : m_nav.framings())
        lines << QStringLiteral("FRAMING %1 '%2'").arg(QString::fromStdString(framing.id), QString::fromStdString(framing.name));
    lines << QStringLiteral("area %1,%2 %3x%4 bar %5,%6 %7x48")
                 .arg(m_area.x()).arg(m_area.y()).arg(m_area.width()).arg(m_area.height())
                 .arg(m_screen.x()).arg(m_screen.bottom() - BAR_H).arg(m_screen.width());
    lines << QStringLiteral("BAR %1").arg(m_barOn);
    return lines.join(QLatin1Char('\n'));
}

void KrellisCanvas::overview() { m_nav.overview(); applyRest(); }
void KrellisCanvas::toggleOverview() { m_nav.toggleOverview(); applyRest(); }
void KrellisCanvas::focusIndex(int i)
{
    if (i >= 0 && i < int(m_windows.size()))
        m_nav.focus(m_windows[size_t(i)].panel);
    applyRest();
}
void KrellisCanvas::toggleMaximize(int i)
{
    if (i >= 0 && i < int(m_windows.size()))
        m_nav.toggle(m_windows[size_t(i)].panel);
    applyRest();
}
void KrellisCanvas::stepOut() { m_nav.stepOut(); applyRest(); }
void KrellisCanvas::stepIn() { m_nav.stepIn(); applyRest(); }
void KrellisCanvas::back() { m_nav.back(); applyRest(); }
void KrellisCanvas::forward() { m_nav.forward(); applyRest(); }
void KrellisCanvas::zoom(double f, double px, double py) { m_nav.zoom(f, {px, py}); }
void KrellisCanvas::finishGesture() { m_nav.finishGesture(); applyRest(); }

bool KrellisEffect::simButton(double x, double y, bool press, int modifiers)
{
    PointerButtonEvent e{};
    e.position = {x, y};
    e.state = press ? PointerButtonState::Pressed : PointerButtonState::Released;
    e.button = Qt::LeftButton;
    e.nativeButton = 272; // BTN_LEFT, matching real libinput events
    e.buttons = press ? Qt::LeftButton : Qt::NoButton;
    e.modifiers = Qt::KeyboardModifiers(modifiers);
    return handleButton(e);
}
bool KrellisEffect::simMiddleButton(double x, double y, bool press)
{
    PointerButtonEvent e{};
    e.position = {x, y};
    e.state = press ? PointerButtonState::Pressed : PointerButtonState::Released;
    e.button = Qt::MiddleButton;
    e.nativeButton = 274; // BTN_MIDDLE
    e.buttons = press ? Qt::MiddleButton : Qt::NoButton;
    return handleButton(e);
}
bool KrellisEffect::simMotion(double x, double y)
{
    input()->pointer()->warp({x, y}); // new windows read the real cursor
    PointerMotionEvent e{};
    e.position = {x, y};
    return handleMotion(e);
}
bool KrellisEffect::simAxis(double delta, double x, double y, int modifiers)
{
    PointerAxisEvent e{};
    e.position = {x, y};
    e.delta = delta;
    e.orientation = Qt::Vertical;
    e.source = PointerAxisSource::Wheel;
    e.modifiers = Qt::KeyboardModifiers(modifiers);
    return handleAxis(e);
}
void KrellisEffect::simPinch(double scale, double x, double y) { pinch(scale, {x, y}, {0, 0}); }
void KrellisEffect::simPinchEnd() { endPinch(); }
void KrellisEffect::simKey(int qtKey, bool press)
{
    KeyboardKeyEvent e{};
    e.key = Qt::Key(qtKey);
    e.state = press ? KeyboardKeyState::Pressed : KeyboardKeyState::Released;
    // The compositor fills the modifier mask from its keyboard state; a simulated key keeps its own.
    Qt::KeyboardModifier flag = Qt::NoModifier;
    switch (e.key) {
    case Qt::Key_Meta: case Qt::Key_Super_L: case Qt::Key_Super_R: flag = Qt::MetaModifier; break;
    case Qt::Key_Alt: flag = Qt::AltModifier; break;
    case Qt::Key_Shift: flag = Qt::ShiftModifier; break;
    case Qt::Key_Control: flag = Qt::ControlModifier; break;
    default: break;
    }
    m_simMods.setFlag(flag, press);
    e.modifiers = m_simMods;
    handleKey(e);
}

void KrellisEffect::simText(const QString &text)
{
    KeyboardKeyEvent event{};
    event.key = Qt::Key_unknown;
    event.state = KeyboardKeyState::Pressed;
    event.text = text;
    handleKey(event);
}

KWIN_EFFECT_FACTORY_SUPPORTED(KrellisEffect, "metadata.json", return krellisKWinMatches() && krellisBootAllowed();)

} // namespace KWin

#include "krellis_effect.moc"
