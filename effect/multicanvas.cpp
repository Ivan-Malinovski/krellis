#include "krellis_effect.h"
#include "effect/effecthandler.h"
#include "core/output.h"
#include "core/renderviewport.h"
#include "window.h"
#include "wayland_server.h"
#include "wayland/seat.h"
#include <QDBusConnection>
#include <QStandardPaths>
#include <QTimer>
#include <KGlobalAccel>
#include <QAction>
#include "input_event_spy.h"
#include <chrono>
#include "shortcut_help.h"
#include "boot_guard.h"
#include "safemode.h"
#include <algorithm>

namespace KWin {

// Global shortcuts run before the effect input filter. Observe releases before
// filtering so a consumed shortcut release can never leave our timer running.
class KrellisKeySpy : public InputEventSpy
{
public:
    explicit KrellisKeySpy(KrellisEffect *effect) : m_effect(effect) {}
    void keyboardKey(KeyboardKeyEvent *event) override { m_effect->observeMoveKey(*event); }
private:
    KrellisEffect *m_effect;
};

// Compile stamp so a running compositor can be told apart from the installed file (see tools/live.sh).
static const char kBuildStamp[] = "build " __DATE__ " " __TIME__; // ASCII so tools can grep the .so
QString KrellisEffect::version() const { return QString::fromLatin1(kBuildStamp); }

static qint64 chordNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

KrellisEffect::KrellisEffect()
{
    krellisBootStarted();
    QTimer::singleShot(int(krellis::BOOT_SURVIVE_MS), this, [] { krellisBootSurvived(); }); // one write, no heartbeat
    m_chordTimer = new QTimer(this);
    m_chordTimer->setSingleShot(true);
    m_chordTimer->setTimerType(Qt::PreciseTimer);
    connect(m_chordTimer, &QTimer::timeout, this, [this] {
        const qint64 now = chordNowMs();
        if (m_chord.tick(now)) showHint();
        else if (m_chord.pending()) m_chordTimer->start(int(std::max<qint64>(1, m_chord.deadline() - now))); // fired a little early
    });
    connect(&m_theme, &krellis::Theme::changed, this, [this] {
        for (const auto &canvas : m_canvases) canvas->themeChanged();
    });
    applySettings(krellis::config::load());
    initializeSession();
    syncOutputs();
    connect(effects, &EffectsHandler::windowAdded, this, [this](EffectWindow *w) {
        syncOutputs();
        watchOutput(w);
        if (w->isPopupWindow()) {
            if (auto *canvas = owner(w)) {
                if (canvas->m_glide) canvas->m_popups[w] = canvas->m_nowMs;
                canvas->schedule();
            }
            return;
        }
        if (KrellisCanvas::manageable(w)) rememberNative(w, false, false);
        for (const auto &canvas : m_canvases)
            if (canvas->m_output == w->screen()) {
                canvas->windowAdded(w);
                sessionWindowOpened();
                break;
            }
    });
    connect(effects, &EffectsHandler::windowActivated, this, [this](EffectWindow *w) {
        if (m_moveWindow && w != m_moveWindow) stopMoveRepeat();
        if (owner(w)) m_selectedOutput = nullptr;
        scratchActivated(w);
    });
    for (auto *window : effects->stackingOrder()) {
        watchOutput(window);
        if (KrellisCanvas::manageable(window)) rememberNative(window, false, false);
        for (const auto &canvas : m_canvases)
            if (canvas->m_output == window->screen())
                canvas->windowAdded(window);
    }
    if (!effects->stackingOrder().isEmpty()) sessionWindowOpened();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &KrellisEffect::syncOutputs);
    timer->start(100);
    m_moveRepeatTimer = new QTimer(this);
    m_moveRepeatTimer->setSingleShot(true);
    m_moveRepeatTimer->setTimerType(Qt::PreciseTimer);
    connect(m_moveRepeatTimer, &QTimer::timeout, this, [this] {
        if (stepMoveRepeat()) m_moveRepeatTimer->start(150);
    });
    m_keySpy = std::make_unique<KrellisKeySpy>(this);
    input()->installInputEventSpy(m_keySpy.get());
    registerShortcuts();
    m_input = std::make_unique<KrellisInput>(this);
    input()->installInputEventFilter(m_input.get());
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Krellis"), this,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllInvokables);
}

KrellisEffect::~KrellisEffect()
{
    krellisBootClean();
    m_shuttingDown = true;
    m_moveRepeatTimer->stop();
    m_keySpy.reset();
    m_saveTimer->stop();
    m_restoreTimer->stop();
    disconnect(effects, nullptr, this, nullptr);
    for (auto &canvas : m_canvases) canvas->prepareUnload();
    if (m_restoreComplete && (m_userTouched || m_pendingUnique == 0)) writeSession();
    input()->uninstallInputEventFilter(m_input.get());
    leaveSurface();
    m_pointerCanvas = m_pinchCanvas = nullptr;
    m_selectedOutput = nullptr;
    restoreNative();
    m_canvases.clear();
    if (waylandServer() && waylandServer()->seat())
        if (auto *native = input()->findToplevel(effects->cursorPos()); native && native->surface())
            waylandServer()->seat()->notifyPointerEnter(native->surface(), effects->cursorPos(), native->inputTransformation());
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/Krellis"));
}

void KrellisEffect::rememberNative(EffectWindow *window, bool automaticMinimize, bool mutation)
{
    auto *native = window->window();
    if (m_shuttingDown || !native || native->isDeleted()) return;
    auto [it, inserted] = m_nativeStates.try_emplace(native, NativeState{native, native->frameGeometry(),
        native->output(), native->keepAbove(), native->keepBelow(), native->isMinimized()});
    if (inserted) {
        connect(native, &QObject::destroyed, this, [this, native] { m_nativeStates.erase(native); });
        connect(native, &Window::minimizedChanged, this, [this, native] {
            // Once explicitly restored, an automatic suppression is no longer ours.
            if (auto state = m_nativeStates.find(native); state != m_nativeStates.end() && !native->isMinimized())
                state->second.automaticMinimize = false;
        });
    }
    it->second.mutated = it->second.mutated || mutation;
    if (automaticMinimize && !native->isMinimized()) it->second.automaticMinimize = true;
}

void KrellisEffect::restoreNative()
{
    // Geometry restoration can emit signals or finish a native interactive
    // operation. Work from guarded copies, with every canvas already quiesced.
    const auto states = m_nativeStates;
    for (const auto &[key, state] : states) {
        auto window = state.window;
        if (!window || window->isDeleted() || !state.mutated) continue;
        disconnect(window, nullptr, this, nullptr);
        if (window->isInteractiveMove() || window->isInteractiveResize()) window->cancelInteractiveMoveResize();
        if (!window || window->isDeleted()) continue;
        window->setKeepAbove(state.keepAbove);
        window->setKeepBelow(state.keepBelow);
        RectF geometry = state.geometry;
        if (!state.output || !effects->screens().contains(state.output)) {
            // An unplugged output cannot receive its old geometry. Keep the
            // original size where possible and place it inside a surviving area.
            if (effects->screens().isEmpty()) continue;
            const auto area = effects->clientArea(MaximizeArea, effects->screens().front());
            geometry.setWidth(std::min(geometry.width(), area.width()));
            geometry.setHeight(std::min(geometry.height(), area.height()));
            geometry.moveLeft(std::clamp(geometry.x(), area.x(), area.right() - geometry.width()));
            geometry.moveTop(std::clamp(geometry.y(), area.y(), area.bottom() - geometry.height()));
        }
        window->moveResize(geometry);
        // Explicit hide/restore remains a native user action after disabling
        // Krellis. Only an automatic singleton suppression is undone.
        if (state.automaticMinimize && !state.minimized && window && !window->isDeleted()) window->setMinimized(false);
    }
    m_nativeStates.clear();
}

// Observe explicit KWin moves between outputs, excluding our camera-driven geometry.
void KrellisEffect::watchOutput(EffectWindow *window)
{
    connect(window, &EffectWindow::windowStartUserMovedResized, this, [this] { markSessionInteraction(); });
    connect(window, &EffectWindow::windowFrameGeometryChanged, this, [this](EffectWindow *w, const RectF &old) {
        if (auto *canvas = owner(w)) canvas->frameChanged(w, old);
    });
    if (!window->window()) return;
    connect(window->window(), &Window::outputChanged, this, [this, window] {
        if (m_shuttingDown) return;
        auto *source = owner(window);
        if (!source || source->m_applyingRest || source->m_session || source->m_resize || source->placedByUs(window)) return;
        QPointer<EffectWindow> guard(window);
        QPointer<LogicalOutput> destination(window->screen());
        QTimer::singleShot(0, this, [this, guard, destination] {
            if (m_shuttingDown || !guard || !destination) return;
            if (auto *current = owner(guard); current && current->placedByUs(guard)) return;
            for (const auto &canvas : m_canvases)
                if (canvas->m_output == destination) {
                    transfer(guard, canvas.get());
                    break;
                }
        });
    });
}

void KrellisEffect::syncOutputs()
{
    if (m_shuttingDown) return;
    const auto outputs = effects->screens();
    for (auto *output : outputs) {
        if (std::any_of(m_canvases.begin(), m_canvases.end(), [output](const auto &c) { return c->m_output == output; }))
            continue;
        auto canvas = std::make_unique<KrellisCanvas>(output);
        canvas->m_theme = &m_theme;
        canvas->m_closeFocus = m_closeFocus;
        canvas->m_floatFirst = m_floatFirst;
        canvas->m_spawnTarget = m_spawnTarget;
        canvas->m_peekPx = m_peekPx;
        canvas->m_scratchpad = m_scratchpad;
        canvas->setPinOn(m_pinOn);
        canvas->setTabsOn(m_tabsOn);
        canvas->m_glide = m_glide;
        canvas->m_landMs = m_landMs;
        canvas->m_iconBelow = m_iconBelow;
        canvas->m_tileMinimum = m_tileMinimum;
        canvas->m_toasts = m_toastsOn;
        canvas->m_shakeFloat = m_shakeFloatOn;
        canvas->m_flyTo = m_flyToOn;
        canvas->m_nativeMutation = [this](EffectWindow *window, bool automatic) { rememberNative(window, automatic); };
        canvas->m_hiddenNativeRect = [this](EffectWindow *window) -> std::optional<RectF> {
            if (auto it = m_nativeStates.find(window->window()); it != m_nativeStates.end()) return it->second.geometry;
            return std::nullopt;
        };
        canvas->m_changed = [this] { scheduleSave(); };
        canvas->m_markedScratch = [this](EffectWindow *window) {
            return std::any_of(m_scratchMarks.begin(), m_scratchMarks.end(), [window](const QPointer<EffectWindow> &p) { return p == window; });
        };
        canvas->m_scratchState = [this] { return scratchState(); };
        canvas->m_scratchAction = [this](const QString &action) {
            if (action == QLatin1String("mark")) toggleScratchMarkActive();
            else toggleScratch(QString());
        };
        canvas->m_toggleMode = [this] { toggleFloatFirst(); };
        canvas->m_mapPinned = m_mapPinned;
        canvas->m_mapOn = m_showMap;
        canvas->m_togglePin = [this] { toggleMapPinned(); };
        canvas->m_transfer = [this](EffectWindow *w, QPointF point) {
            if (auto *target = canvasAt(point))
                transfer(w, target);
        };
        canvas->m_handoff = [this](EffectWindow *w, QPointF point) {
            auto *target = canvasAt(point);
            auto *source = owner(w);
            if (!target || !source || target == source || !source->m_session || source->m_dragWindow != w)
                return false;
            const QRectF start = source->m_dragStartGeometry;
            source->endDrag(false);
            transfer(w, target);
            target->adoptDrag(w, point, start);
            return true;
        };
        m_canvases.push_back(std::move(canvas));
    }
    for (auto it = m_canvases.begin(); it != m_canvases.end();) {
        auto *removed = it->get();
        if (outputs.contains(removed->m_output)) {
            ++it;
            continue;
        }
        if (m_selectedOutput == removed->m_output) m_selectedOutput = nullptr;
        if (m_pointerCanvas == removed) m_pointerCanvas = nullptr;
        if (m_pinchCanvas == removed) m_pinchCanvas = nullptr;
        KrellisCanvas *target = nullptr;
        for (const auto &c : m_canvases)
            if (outputs.contains(c->m_output)) { target = c.get(); break; }
        if (!target) { ++it; continue; } // Preserve documents until an output is available.
        auto windows = removed->m_windows;
        windows.insert(windows.end(), removed->m_stowed.begin(), removed->m_stowed.end()); // tabs part company here
        removed->m_session.reset();
        removed->m_dragWindow = nullptr;
        for (const auto &t : windows)
            transfer(t.window, target);
        it = m_canvases.erase(it);
        effects->addRepaintFull();
    }
}

KrellisCanvas *KrellisEffect::owner(EffectWindow *w) const
{
    for (const auto &canvas : m_canvases)
        if (canvas->trackedFor(w) || canvas->stowedFor(w) || canvas->m_closing.count(w))
            return canvas.get();
    if (w && w->isPopupWindow()) {
        auto *parent = w->transientFor();
        if (parent && parent != w) return owner(parent);
    }
    return nullptr;
}

KrellisCanvas *KrellisEffect::canvasAt(QPointF point) const
{
    for (const auto &canvas : m_canvases)
        if (effects->screens().contains(canvas->m_output) && QRectF(QRect(canvas->m_output->geometry())).contains(point))
            return canvas.get();
    return nullptr;
}

KrellisCanvas *KrellisEffect::current() const
{
    for (const auto &canvas : m_canvases)
        if (canvas->m_output == m_selectedOutput)
            return canvas.get();
    if (auto *canvas = owner(effects->activeWindow())) return canvas;
    if (auto *canvas = canvasAt(effects->cursorPos())) return canvas;
    return m_canvases.empty() ? nullptr : m_canvases.front().get();
}

void KrellisCanvas::detachWindow(EffectWindow *w)
{
    auto *tracked = trackedFor(w);
    if (!tracked) tracked = stowedFor(w);
    if (tracked) {
        if (m_resize) finishResize(false);
        const std::string view = tracked->view;
        if (m_dragWindow == w) {
            m_session.reset();
            m_dragWindow = nullptr;
            m_dragFromKWin = false;
        }
        m_flights.erase(w);
        m_doc = krellis::closeView(m_doc, view);
        std::erase_if(m_windows, [w](const auto &t) { return t.window == w; });
        std::erase_if(m_stowed, [w](const auto &t) { return t.window == w; });
        m_dragTabView.clear();
        m_tabPress.reset();
        relayout(true);
        updateDock();
    }
}

void KrellisEffect::transfer(EffectWindow *w, KrellisCanvas *target)
{
    auto *source = owner(w);
    if (!source || source == target) return;
    auto *tracked = source->trackedFor(w);
    if (!tracked) tracked = source->stowedFor(w);
    if (!tracked) return;
    if (source->m_resize) source->finishResize(false);
    if (target->m_resize) target->finishResize(false);
    TRect from = source->screenRect(tracked->panel);
    double opacity = 1;
    source->flightRect(w, *tracked, from, opacity);
    const bool hidden = w->isMinimized();
    source->detachWindow(w);
    target->windowAdded(w, true);
    if (hidden) target->minimizedChanged(w);
    else {
        target->startFlight(w, KrellisCanvas::FlightKind::Glide, from, {}, opacity);
        target->m_flights.at(w).crossOutput = true;
    }
    target->updateDock();
}

void KrellisEffect::prePaintScreen(ScreenPrePaintData &data)
{
    // Surface damage arrives in native coordinates, while every managed window
    // can be painted elsewhere. The simple path cannot transform that damage.
    data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    data.paint = Region::infinite();
    for (const auto &canvas : m_canvases) canvas->prePaintScreen(data);
    effects->prePaintScreen(data);
}
void KrellisEffect::postPaintScreen()
{
    for (const auto &canvas : m_canvases) canvas->postPaintScreen();
    effects->postPaintScreen();
}
void KrellisEffect::prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data)
{
    if (auto *canvas = owner(w)) canvas->prePaintWindow(view, w, data);
    effects->prePaintWindow(view, w, data);
}
void KrellisEffect::paintWindow(const RenderTarget &rt, const RenderViewport &vp, EffectWindow *w, int mask, const Region &region, WindowPaintData &data)
{
    if (auto *canvas = owner(w)) {
        // A zoomed or overshooting canvas must not paint onto its neighbour.
        if (!canvas->m_output || !effects->screens().contains(canvas->m_output))
            return;
        const auto flight = canvas->m_flights.find(w);
        const bool crossing = flight != canvas->m_flights.end() && flight->second.crossOutput;
        const Region clipped = crossing ? region : region & vp.mapToDeviceCoordinatesAligned(Region(canvas->m_output->geometry()));
        canvas->paintWindow(rt, vp, w, mask, clipped, data);
    } else {
        effects->paintWindow(rt, vp, w, mask, region, data);
    }
}

bool KrellisEffect::handleButton(const PointerButtonEvent &e)
{
    if (m_whichKey && (m_chord.pending() || m_chord.shown())) hideHint();
    if (e.state == PointerButtonState::Pressed) markSessionInteraction();
    if (auto *active = effects->activeWindow(); active && (active->isUserMove() || active->isUserResize())) {
        // A decoration press was delivered by us, so its release belongs to the
        // same painted window even when KWin's native hit is now elsewhere.
        if (m_clientGrab && m_grabDecoration && e.state == PointerButtonState::Released)
            return surfaceButton(e);
        return false; // KWin's interactive-move filter follows Effects in the chain.
    }
    if (paletteButton(e)) return true;
    if (pinButton(e)) return true;
    if (e.state == PointerButtonState::Pressed) scratchClick(e.position);
    if (m_clientGrab && !m_pointerCanvas && !m_pinchCanvas) return surfaceButton(e);
    auto *canvas = m_pointerCanvas ? m_pointerCanvas : canvasAt(e.position);
    if (!canvas) return surfaceButton(e);
    const bool handled = canvas->handleButton(e);
    if (handled) {
        leaveSurface();
        if (canvas->m_naming)
            for (const auto &other : m_canvases)
                if (other.get() != canvas && other->m_naming) { other->m_naming = false; other->updateBar(); }
    }
    if (e.state == PointerButtonState::Released) m_pointerCanvas = nullptr;
    else if (handled) {
        m_pointerCanvas = canvas;
        m_selectedOutput = canvas->m_output;
    }
    return handled || surfaceButton(e);
}
// While the palette is open every click belongs to it: a row jumps, the card itself is inert, anywhere else closes it.
// The release of a click it took is swallowed too, so it cannot land on the window underneath.
bool KrellisEffect::paletteButton(const PointerButtonEvent &e)
{
    if (e.state == PointerButtonState::Released) {
        const bool swallow = m_swallowRelease;
        m_swallowRelease = false;
        return swallow;
    }
    auto *palette = paletteCanvas();
    if (!palette) return false;
    m_swallowRelease = true;
    leaveSurface();
    const QString hit = palette->paletteHit(e.position);
    if (hit.startsWith(QLatin1String("row:")))
        palette->paletteGo(palette->m_paletteFirst + hit.mid(4).toInt());
    else if (hit.isEmpty())
        palette->closePalette();
    return true;
}
KrellisCanvas *KrellisEffect::paletteCanvas() const
{
    for (const auto &canvas : m_canvases)
        if (canvas->paletteOpen()) return canvas.get();
    return nullptr;
}

// Everything the palette can jump to, most recently used first, with the window you are in last so that opening the
// palette and pressing Return goes back to the previous one. Saved views of every output follow the windows.
void KrellisEffect::togglePalette()
{
    markSessionInteraction();
    if (auto *open = paletteCanvas()) {
        open->closePalette();
        return;
    }
    auto *canvas = current();
    if (!canvas || canvas->m_overlaysCovered || canvas->m_naming || m_resizeMode) return;
    for (const auto &c : m_canvases)
        if (c->m_drag != KrellisCanvas::Drag::None || c->m_session || c->m_nav.gestureActive()) return;
    const bool several = m_canvases.size() > 1;
    std::vector<KrellisCanvas::PaletteItem> items;
    EffectWindow *active = effects->activeWindow();
    auto add = [&](EffectWindow *w) {
        auto *home = owner(w);
        if (!home || w->isDeleted()) return;
        const auto *tracked = home->trackedFor(w);
        if (!tracked) tracked = home->stowedFor(w); // a tab behind another one
        if (!tracked) return;
        const std::string panel = tracked->panel;
        QStringList detail;
        if (w->isMinimized() && !home->scratchWindow(w)) detail << QStringLiteral("hidden");
        if (home->scratchWindow(w)) detail << QStringLiteral("scratchpad");
        else if (home->m_drawers.count(panel)) detail << QStringLiteral("drawer");
        else if (home->floatScreen(panel)) detail << QStringLiteral("floating");
        if (several) detail << home->m_output->name();
        QString app = KrellisCanvas::appId(w);
        app = app.mid(app.lastIndexOf(QLatin1Char('.')) + 1);
        KrellisCanvas::PaletteItem item{w->caption(), app, detail.join(QStringLiteral(" · ")), false, {}};
        item.go = [this, w = QPointer<EffectWindow>(w), home = QPointer<KrellisCanvas>(home)] {
            if (!w || !home) return;
            markSessionInteraction();
            m_selectedOutput = home->m_output;
            if (home->scratchWindow(w)) {
                m_scratchLast = w;
                showScratch(scratchWindows());
            }
            home->jumpTo(w);
        };
        items.push_back(std::move(item));
    };
    const auto stack = effects->stackingOrder();
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
        if (*it != active) add(*it);
    if (active) add(active);
    for (const auto &c : m_canvases)
        for (const auto &framing : c->m_nav.framings()) {
            KrellisCanvas::PaletteItem item{QString::fromStdString(framing.name), {}, several ? c->m_output->name() : QString(), true, {}};
            item.go = [this, home = QPointer<KrellisCanvas>(c.get()), id = framing.id] {
                if (!home) return;
                markSessionInteraction();
                m_selectedOutput = home->m_output;
                home->m_nav.goFraming(id);
                home->applyRest();
            };
            items.push_back(std::move(item));
        }
    canvas->openPalette(std::move(items));
}
// A pinned window's thumbnail is a button: a click reveals the tile it stands in for, and neither press nor release
// reaches any window.
EffectWindow *KrellisEffect::pinUnder(QPointF p) const
{
    if (m_clientGrab || m_pointerCanvas) return nullptr;
    auto *canvas = canvasAt(p);
    return canvas ? canvas->pinAt(p) : nullptr;
}
bool KrellisEffect::pinButton(const PointerButtonEvent &e)
{
    if (e.state == PointerButtonState::Released) {
        const bool swallow = m_swallowRelease;
        m_swallowRelease = false;
        return swallow;
    }
    EffectWindow *w = pinUnder(e.position);
    if (!w) return false;
    m_swallowRelease = true;
    leaveSurface();
    if (e.button == Qt::LeftButton) {
        markSessionInteraction();
        auto *home = owner(w);
        m_selectedOutput = home->m_output;
        home->jumpTo(w);
    }
    return true;
}
bool KrellisEffect::handleMotion(const PointerMotionEvent &e)
{
    for (const auto &canvas : m_canvases) canvas->m_shiftHeld = e.modifiers & Qt::ShiftModifier;
    if (auto *active = effects->activeWindow(); active && (active->isUserMove() || active->isUserResize())) {
        if (active->isUserResize())
            if (auto *canvas = owner(active)) canvas->resizeAt(active, e.position);
        return false; // KWin still owns the native interactive grab.
    }
    if (auto *palette = paletteCanvas()) {
        leaveSurface();
        const QString hit = palette->paletteHit(e.position);
        if (hit.startsWith(QLatin1String("row:"))) palette->paletteHover(hit.mid(4).toInt());
        return true;
    }
    if (m_pointerCanvas) {
        leaveSurface();
        return m_pointerCanvas->handleMotion(e);
    }
    if (!m_clientGrab && pinUnder(e.position)) { // a thumbnail is a button, not a window: nothing underneath should see the pointer
        leaveSurface();
        return true;
    }
    if (m_clientGrab) return surfaceMotion(e);
    bool handled = false;
    for (const auto &canvas : m_canvases) handled = canvas->handleMotion(e) || handled;
    return handled || surfaceMotion(e);
}
bool KrellisEffect::handleAxis(const PointerAxisEvent &e)
{
    if (auto *active = effects->activeWindow(); active && (active->isUserMove() || active->isUserResize()))
        return false; // KWin's interactive-move filter follows Effects in the chain.
    if (auto *palette = paletteCanvas()) {
        if (e.orientation == Qt::Vertical) palette->paletteScroll(e.delta, e.source == PointerAxisSource::Wheel);
        return true;
    }
    if (pinUnder(e.position)) return true;
    auto *canvas = canvasAt(e.position);
    // Scrolling over a server-side title bar or a gap between tiles zooms without any key held.
    bool titleBar = false;
    if (canvas && !m_clientGrab) {
        if (const auto hit = surfaceHit(e.position); hit && hit->decoration && !hit->inert && hit->canvas == canvas
            && hit->window->window())
            titleBar = hit->mapped.y() < hit->window->window()->clientGeometry().top();
    }
    // The gap between two tiles belongs to nobody, so it scrolls too (and works for windows with no title bar). It
    // favours the right/lower tile; Shift favours the left/upper one.
    if (canvas && !m_clientGrab && !titleBar)
        if (const auto seam = canvas->seamScrollPoint(e.position, e.modifiers & Qt::ShiftModifier)) {
            auto onSeam = e;
            onSeam.position = *seam;
            return canvas->handleAxis(onSeam, true) || surfaceAxis(e);
        }
    return (canvas && canvas->handleAxis(e, titleBar)) || surfaceAxis(e);
}
// Follows Meta and Alt (and Shift/Ctrl, which cancel the hold) from the key stream. Never consumes anything.
// The flags are re-read from the event's modifier mask every time, so a release swallowed by a filter ahead of us
// (TabBox, a grab) cannot leave one stuck; the event's own key wins because the mask may be sampled before or after it.
void KrellisEffect::trackChord(const KeyboardKeyEvent &e)
{
    if (!m_whichKey) return;
    const bool pressed = e.state != KeyboardKeyState::Released;
    bool meta = e.modifiers.testFlag(Qt::MetaModifier), alt = e.modifiers.testFlag(Qt::AltModifier);
    bool shift = e.modifiers.testFlag(Qt::ShiftModifier), ctrl = e.modifiers.testFlag(Qt::ControlModifier);
    bool *own = nullptr;
    switch (e.key) {
    case Qt::Key_Meta: case Qt::Key_Super_L: case Qt::Key_Super_R: own = &meta; break;
    case Qt::Key_Alt: own = &alt; break;
    case Qt::Key_Shift: own = &shift; break;
    case Qt::Key_Control: own = &ctrl; break;
    default: break;
    }
    if (!own) {
        if (pressed) hideHint();
        return;
    }
    if (e.state == KeyboardKeyState::Repeated) return;
    *own = pressed;
    const qint64 now = chordNowMs();
    m_chord.setModifiers(meta, alt, shift || ctrl, now);
    if (pressed && (own == &shift || own == &ctrl)) {
        hideHint(); // Meta+Alt+Shift is a chord of its own; releasing Shift must not start a new hold
    } else if (m_chord.pending()) {
        m_chordTimer->start(int(std::max<qint64>(0, m_chord.deadline() - now)));
    } else if (!m_chord.shown()) {
        hideHintUi();
    }
}

void KrellisEffect::hideHintUi()
{
    m_chordTimer->stop();
    for (const auto &canvas : m_canvases) canvas->hideSheet();
}

void KrellisEffect::hideHint()
{
    m_chord.dismiss();
    hideHintUi();
}

void KrellisEffect::showHint()
{
    if (auto *canvas = current()) canvas->showSheet(helpGroups());
}

QVariantList KrellisEffect::helpGroups() const
{
    auto rowsOf = [this](const krellis::HelpRow *rows, int count) {
        QVariantList out;
        for (int i = 0; i < count; ++i) {
            const auto *action = findChild<QAction *>(QString::fromLatin1(rows[i].id), Qt::FindDirectChildrenOnly);
            if (!action) continue;
            const auto keys = KGlobalAccel::self()->shortcut(action);
            if (keys.isEmpty()) continue;
            QString text = keys.first().toString(QKeySequence::NativeText);
            if (rows[i].arrows) {
                const qsizetype plus = text.lastIndexOf(QLatin1Char('+'));
                text = (plus >= 0 ? text.left(plus + 1) : QString()) + QStringLiteral("Arrows");
            }
            out << QVariantMap{{QStringLiteral("label"), QString::fromLatin1(rows[i].label)}, {QStringLiteral("keys"), text}};
        }
        return out;
    };
    QVariantList groups;
    for (const auto &group : krellis::kHelpGroups)
        groups << QVariantMap{{QStringLiteral("name"), QString::fromLatin1(group.name)}, {QStringLiteral("rows"), rowsOf(group.rows, group.count)}};
    if (m_scratchpad)
        groups << QVariantMap{{QStringLiteral("name"), QStringLiteral("Scratchpad")},
                              {QStringLiteral("rows"), rowsOf(krellis::kHelpScratch, int(sizeof(krellis::kHelpScratch) / sizeof(krellis::HelpRow)))}};
    return groups;
}

bool KrellisEffect::handleKey(const KeyboardKeyEvent &e)
{
    trackChord(e);
    // Shift floats a dragged window, and pressing or releasing it must count at once, not on the next pointer motion
    // (a held-still drag would otherwise show no change). This runs before the native-move early return below.
    {
        bool shift = e.modifiers.testFlag(Qt::ShiftModifier);
        if (e.key == Qt::Key_Shift) shift = e.state != KeyboardKeyState::Released;
        for (const auto &canvas : m_canvases) {
            if (canvas->m_shiftHeld == shift) continue;
            canvas->m_shiftHeld = shift;
            if (canvas->m_session) canvas->moveDrag(canvas->m_windowPointer);
        }
    }
    if (auto *active = effects->activeWindow(); active && (active->isUserMove() || active->isUserResize())) {
        if (e.key == Qt::Key_Escape && e.state == KeyboardKeyState::Pressed)
            if (auto *canvas = owner(active); canvas && canvas->m_resize) canvas->m_resize->decision = false;
        return false; // Let KWin handle Escape, Return and keyboard resize steps.
    }
    if (e.state == KeyboardKeyState::Pressed) markSessionInteraction();
    if (m_resizeMode) return handleResizeKey(e);
    KrellisCanvas *editing = nullptr;
    for (const auto &canvas : m_canvases) if (canvas->capturesKeys()) { editing = canvas.get(); break; }
    if (e.state == KeyboardKeyState::Released && m_inertKeys.erase(e.key)) {
        if (editing) editing->handleKey(e);
        return true;
    }
    if (editing) {
        if (e.state != KeyboardKeyState::Released) m_inertKeys.insert(e.key);
        return editing->handleKey(e);
    }
    bool handled = false, busy = false;
    for (const auto &canvas : m_canvases) {
        handled = canvas->handleKey(e) || handled;
        busy = busy || canvas->m_nav.gestureActive() || canvas->m_drag != KrellisCanvas::Drag::None || bool(canvas->m_session);
    }
    if (e.state == KeyboardKeyState::Released && m_inertKeys.erase(e.key)) return true;
    if (busy && e.state != KeyboardKeyState::Released) {
        m_inertKeys.insert(e.key);
        return true;
    }
    return handled;
}
void KrellisEffect::pinch(double scale, QPointF at, QPointF delta)
{
    if (!m_pinchCanvas) m_pinchCanvas = canvasAt(at);
    leaveSurface();
    if (m_pinchCanvas) m_pinchCanvas->pinch(scale, at, delta);
}
void KrellisEffect::endPinch()
{
    if (m_pinchCanvas) m_pinchCanvas->endPinch();
    m_pinchCanvas = nullptr;
}
QString KrellisEffect::barHit(double x, double y)
{
    auto *canvas = canvasAt({x, y});
    return canvas ? canvas->barKey({x, y}) : QString();
}
QString KrellisEffect::dump()
{
    if (m_canvases.size() == 1) return m_canvases.front()->dump();
    QStringList lines;
    for (size_t i = 0; i < m_canvases.size(); ++i)
        lines << QStringLiteral("OUTPUT %1 %2\n%3").arg(i).arg(m_canvases[i]->m_output->name(), m_canvases[i]->dump());
    return lines.join(QLatin1Char('\n'));
}
QString KrellisEffect::dumpOutput(int i)
{
    return i >= 0 && i < int(m_canvases.size()) ? m_canvases[size_t(i)]->dump() : QString();
}
void KrellisEffect::selectOutput(int i)
{
    m_selectedOutput = i >= 0 && i < int(m_canvases.size()) ? m_canvases[size_t(i)]->m_output : nullptr;
}
void KrellisEffect::moveToOutput(int window, int output)
{
    markSessionInteraction();
    auto *canvas = current();
    if (!canvas || window < 0 || window >= int(canvas->m_windows.size()) || output < 0 || output >= int(m_canvases.size())) return;
    transfer(canvas->m_windows[size_t(window)].window, m_canvases[size_t(output)].get());
}
bool KrellisEffect::simDragStart(int i, double x, double y)
{
    auto *canvas = current();
    if (!canvas || !canvas->simDragStart(i, x, y)) return false;
    m_pointerCanvas = canvas;
    return true;
}
void KrellisEffect::simDragMove(double x, double y)
{
    if (m_pointerCanvas) m_pointerCanvas->simDragMove(x, y);
}
void KrellisEffect::simForceFloat(int mode)
{
    for (const auto &canvas : m_canvases) canvas->m_simForceFloat = mode;
}
void KrellisEffect::simDragEnd(bool commit)
{
    auto *canvas = m_pointerCanvas;
    m_pointerCanvas = nullptr;
    if (canvas) canvas->simDragEnd(commit);
}
void KrellisEffect::overview() { markSessionInteraction(); if (auto *canvas = current()) canvas->overview(); }
void KrellisEffect::toggleOverview() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleOverview(); }
void KrellisEffect::focusIndex(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->focusIndex(index); }
void KrellisEffect::toggleMaximize(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleMaximize(index); }
void KrellisEffect::toggleHide(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleHide(index); }
void KrellisEffect::toggleFloat(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleFloat(index); }
void KrellisEffect::stepOut() { markSessionInteraction(); if (auto *canvas = current()) canvas->stepOut(); }
void KrellisEffect::stepIn() { markSessionInteraction(); if (auto *canvas = current()) canvas->stepIn(); }
void KrellisEffect::nextPanel() { markSessionInteraction(); if (auto *canvas = current()) canvas->nextPanel(); }
void KrellisEffect::previousPanel() { markSessionInteraction(); if (auto *canvas = current()) canvas->previousPanel(); }
void KrellisEffect::cyclePanel(int step) { markSessionInteraction(); if (auto *canvas = current()) canvas->cyclePanel(step); }
void KrellisEffect::toggleMaximizeActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleMaximizeActive(); }
void KrellisEffect::toggleFloatActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleFloatActive(); }
void KrellisEffect::pinActive() { if (!m_pinOn) return; markSessionInteraction(); if (auto *canvas = current()) canvas->togglePinActive(); }
void KrellisEffect::stashActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->stashActive(); }
void KrellisEffect::flipActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->flipActive(); }
void KrellisEffect::toggleHideActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleHideActive(); }
std::vector<EffectWindow *> KrellisEffect::scratchWindows() const
{
    std::vector<EffectWindow *> out;
    for (const auto &canvas : m_canvases)
        for (const auto &t : canvas->m_windows)
            if (canvas->scratchWindow(t.window)) out.push_back(t.window);
    return out;
}
int KrellisEffect::scratchCount() const { return int(scratchWindows().size()); }
QVariantMap KrellisEffect::scratchState() const
{
    const auto windows = scratchWindows();
    QVariantMap m;
    m[QStringLiteral("enabled")] = m_scratchpad;
    m[QStringLiteral("count")] = int(windows.size());
    m[QStringLiteral("shown")] = std::any_of(windows.begin(), windows.end(), [](EffectWindow *w) { return !w->isMinimized(); });
    EffectWindow *active = effects->activeWindow();
    m[QStringLiteral("active")] = active && std::find(windows.begin(), windows.end(), active) != windows.end();
    m[QStringLiteral("canMark")] = active && owner(active);
    return m;
}
void KrellisEffect::refreshScratchUi()
{
    for (const auto &canvas : m_canvases) {
        canvas->updateDock();
        canvas->updateMap(true);
    }
}
bool KrellisEffect::toggleScratchMarkActive()
{
    if (!m_scratchpad) return false;
    markSessionInteraction();
    EffectWindow *w = effects->activeWindow();
    auto *canvas = w ? owner(w) : nullptr;
    if (!canvas) return false;
    const auto marked = std::find_if(m_scratchMarks.begin(), m_scratchMarks.end(), [w](const QPointer<EffectWindow> &p) { return p == w; });
    if (marked != m_scratchMarks.end()) {
        m_scratchMarks.erase(marked); // stays a float; Meta+Shift+D docks it again
        refreshScratchUi();
        return true;
    }
    if (canvas->scratchWindow(w)) return false; // flagged by a rule in krellisrc
    m_scratchMarks.erase(std::remove_if(m_scratchMarks.begin(), m_scratchMarks.end(), [](const QPointer<EffectWindow> &p) { return p.isNull(); }), m_scratchMarks.end());
    const int slot = int(std::count_if(m_scratchMarks.begin(), m_scratchMarks.end(), [](const QPointer<EffectWindow> &p) { return !p.isNull(); }));
    if (!canvas->floatForScratch(w, slot)) return false;
    m_scratchMarks.emplace_back(w);
    refreshScratchUi();
    return true;
}
bool KrellisEffect::scratchLayerShown() const
{
    return std::any_of(m_canvases.begin(), m_canvases.end(), [](const auto &canvas) { return canvas->layerShown(); });
}
// One shortcut for the whole layer: if any scratch window is up they all go away together, otherwise they all come up
// together on the output under the pointer, in the positions they were left in, the one used last on top. A name picks
// out the windows of one app (or caption) for a shortcut of its own.
void KrellisEffect::toggleScratch(const QString &name)
{
    markSessionInteraction();
    auto windows = scratchWindows();
    if (!name.isEmpty()) {
        const QString needle = name.toLower();
        windows.erase(std::remove_if(windows.begin(), windows.end(), [&](EffectWindow *w) {
            return KrellisCanvas::appId(w).toLower() != needle && !w->caption().toLower().contains(needle);
        }), windows.end());
    }
    if (windows.empty()) return;
    const bool up = std::any_of(windows.begin(), windows.end(), [](EffectWindow *w) { return !w->isMinimized(); });
    if (up) hideScratch(windows);
    else showScratch(windows);
    refreshScratchUi();
    toast(up ? QStringLiteral("Scratchpad hidden") : QStringLiteral("Scratchpad shown"));
}
void KrellisEffect::hideScratch(const std::vector<EffectWindow *> &windows)
{
    m_scratchBusy = true;
    for (EffectWindow *w : windows)
        if (!w->isMinimized()) w->minimize();
    m_scratchBusy = false;
}
void KrellisEffect::showScratch(const std::vector<EffectWindow *> &windows)
{
    m_scratchBusy = true;
    auto *target = canvasAt(effects->cursorPos());
    for (EffectWindow *w : windows) {
        if (target && owner(w) != target) transfer(w, target);
        w->unminimize();
    }
    // Raise in turn so the window used last ends up on top and keeps the focus.
    EffectWindow *top = nullptr;
    for (EffectWindow *w : windows) if (w == m_scratchLast) top = w;
    for (EffectWindow *w : windows) if (w != top) effects->activateWindow(w);
    effects->activateWindow(top ? top : windows.back());
    m_scratchBusy = false;
}
// Focus moving to a tiled window outside the layer (alt-tab, a click, a shortcut) puts the layer away; a dialog of a
// scratch window, or anything the effect does not manage, leaves it alone.
void KrellisEffect::scratchActivated(EffectWindow *w)
{
    if (!w || m_scratchBusy) return;
    const auto windows = scratchWindows();
    if (windows.empty()) return;
    if (std::find(windows.begin(), windows.end(), w) != windows.end()) {
        m_scratchLast = w;
        return;
    }
    if (!owner(w)) return;
    for (EffectWindow *parent = w->transientFor(); parent; parent = parent->transientFor())
        if (std::find(windows.begin(), windows.end(), parent) != windows.end()) return;
    if (!scratchLayerShown()) return;
    hideScratch(windows);
    refreshScratchUi();
}
// A click on bare canvas, with no window of any kind under it, also puts the layer away. The bar and the minimap
// are the effect's own surfaces and keep their buttons.
void KrellisEffect::scratchClick(QPointF point)
{
    if (m_scratchBusy || !scratchLayerShown()) return;
    auto *canvas = canvasAt(point);
    if (!canvas || !canvas->layerShown() || canvas->overUi(point) || surfaceHit(point)) return;
    for (const auto *w : effects->stackingOrder())
        if (!w->isDeleted() && !w->isMinimized() && !w->isHidden() && !w->isDesktop() && w->frameGeometry().contains(point)) return;
    hideScratch(scratchWindows());
    refreshScratchUi();
}

static std::optional<krellis::Edge> parseDirection(const QString &name)
{
    const QString d = name.trimmed().toLower();
    if (d == QLatin1String("left")) return krellis::Edge::Left;
    if (d == QLatin1String("right")) return krellis::Edge::Right;
    if (d == QLatin1String("up") || d == QLatin1String("top")) return krellis::Edge::Top;
    if (d == QLatin1String("down") || d == QLatin1String("bottom")) return krellis::Edge::Bottom;
    return std::nullopt;
}
void KrellisEffect::focusDirection(const QString &direction)
{
    markSessionInteraction();
    if (const auto dir = parseDirection(direction)) if (auto *canvas = current()) canvas->focusDirection(*dir);
}
void KrellisEffect::groupDirection(const QString &direction)
{
    markSessionInteraction();
    const auto dir = parseDirection(direction);
    if (auto *canvas = dir ? current() : nullptr) canvas->groupWithNeighbour(*dir);
}
void KrellisEffect::observeMoveKey(const KeyboardKeyEvent &e)
{
    if (e.state == KeyboardKeyState::Repeated) return;
    const auto mods = e.modifiers & (Qt::MetaModifier | Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier);
    if (m_moveKey && (e.key == m_moveKey || mods != m_moveMods || e.state == KeyboardKeyState::Pressed))
        stopMoveRepeat();
    if (e.state == KeyboardKeyState::Pressed) {
        m_shortcutDown = true;
        m_shortcutKey = e.key;
        m_shortcutPhysicalMods = mods;
        m_shortcutMods = e.modifiersRelevantForGlobalShortcuts;
        ++m_keyPressSerial;
    } else if (e.key == m_shortcutKey || mods != m_shortcutPhysicalMods) {
        m_shortcutDown = false;
    }
}

void KrellisEffect::startMoveShortcut(QAction *action, const QString &direction)
{
    // Match the user's actual binding, including remapped alternatives. Delayed
    // global-shortcut notifications after release still perform a tap, never a hold.
    bool matches = false;
    for (const auto &chord : KGlobalAccel::self()->shortcut(action))
        if (chord.count() == 1 && chord[0] == QKeyCombination(m_shortcutMods, Qt::Key(m_shortcutKey))) matches = true;
    if (!matches || m_consumedMovePress == m_keyPressSerial) return;
    m_consumedMovePress = m_keyPressSerial;
    stopMoveRepeat();
    m_moveKey = m_shortcutKey;
    m_moveMods = m_shortcutPhysicalMods;
    m_moveDirection = direction;
    m_moveWindow = effects->activeWindow();
    if (stepMoveRepeat()) {
        if (m_shortcutDown) m_moveRepeatTimer->start(320);
        else stopMoveRepeat();
    }
}

void KrellisEffect::stopMoveRepeat()
{
    if (m_moveRepeatTimer) m_moveRepeatTimer->stop();
    m_moveKey = 0;
    m_moveWindow = nullptr;
    for (const auto &canvas : m_canvases) if (canvas->m_keyMoveHeld) {
        canvas->m_keyMoveHeld = false;
        canvas->m_keyMoveReleased = canvas->m_nowMs;
        canvas->schedule();
    }
}

bool KrellisEffect::stepMoveRepeat()
{
    auto *canvas = owner(m_moveWindow);
    if (!m_moveKey || !canvas || effects->activeWindow() != m_moveWindow || effects->isScreenLocked()
        || (waylandServer() && waylandServer()->isKeyboardShortcutsInhibited())
        || m_resizeMode || canvas->capturesKeys() || canvas->m_session
        || canvas->m_drag != KrellisCanvas::Drag::None || canvas->m_nav.gestureActive()
        || m_moveWindow->isUserMove() || m_moveWindow->isUserResize()) {
        stopMoveRepeat();
        return false;
    }
    moveDirection(m_moveDirection);
    // A step can carry the same window to another output. Move the outline with it.
    auto *destination = owner(m_moveWindow);
    if (!destination) { stopMoveRepeat(); return false; }
    for (const auto &other : m_canvases) if (other.get() != destination && other->m_keyMoveHeld) {
        other->m_keyMoveHeld = false;
        other->m_keyMoveReleased = other->m_nowMs;
        other->schedule();
    }
    if (auto *tracked = destination->trackedFor(m_moveWindow)) {
        destination->m_keyMovePanel = tracked->panel;
        destination->m_keyMoveHeld = true;
        destination->schedule();
    }
    return true;
}

void KrellisEffect::moveDirection(const QString &direction)
{
    markSessionInteraction();
    const auto dir = parseDirection(direction);
    auto *canvas = dir ? current() : nullptr;
    if (!canvas || canvas->moveDirection(*dir)) return;
    // Already at the edge of this output: carry the window on to the output beside it, entering at the near edge.
    auto *window = effects->activeWindow();
    auto *target = outputBeside(canvas, *dir);
    if (!window || owner(window) != canvas) return;
    // No output that way either: the tile has nowhere to go, so it gives up 20% of its size instead.
    if (!target) { canvas->resizeStep(*dir, 0.2); return; }
    transfer(window, target);
    target->enterFrom(window, krellis::oppositeEdge(*dir));
    effects->activateWindow(window);
}
// The canvas whose output lies next to `from`'s on the `dir` side: it must share some of its extent, and the
// nearest one wins (then the one sharing most).
KrellisCanvas *KrellisEffect::outputBeside(KrellisCanvas *from, krellis::Edge dir) const
{
    const QRectF g(from->m_output->geometry());
    const bool horizontal = krellis::edgeAxis(dir) == krellis::Axis::X;
    KrellisCanvas *best = nullptr;
    double bestGap = 0, bestOverlap = 0;
    for (const auto &canvas : m_canvases) {
        if (canvas.get() == from || !effects->screens().contains(canvas->m_output)) continue;
        const QRectF r(canvas->m_output->geometry());
        const double gap = dir == krellis::Edge::Right ? r.left() - g.right() : dir == krellis::Edge::Left ? g.left() - r.right()
                         : dir == krellis::Edge::Bottom ? r.top() - g.bottom() : g.top() - r.bottom();
        const double overlap = horizontal ? std::min(g.bottom(), r.bottom()) - std::max(g.top(), r.top())
                                          : std::min(g.right(), r.right()) - std::max(g.left(), r.left());
        if (gap < -1 || overlap <= 0) continue;
        if (!best || gap < bestGap - 1 || (std::abs(gap - bestGap) <= 1 && overlap > bestOverlap)) {
            best = canvas.get();
            bestGap = gap;
            bestOverlap = overlap;
        }
    }
    return best;
}
bool KrellisEffect::resizeStep(const QString &direction, double fraction)
{
    markSessionInteraction();
    const auto dir = parseDirection(direction);
    auto *canvas = current();
    return dir && canvas && canvas->resizeStep(*dir, std::clamp(fraction, 0.005, 0.5));
}
// Resize mode swallows the keyboard so arrows resize the tile instead of reaching the application. It leaves on
// Escape, Return, the shortcut again, or after a few idle seconds, so it can never trap the keyboard.
void KrellisEffect::setResizeMode(bool on)
{
    if (on == m_resizeMode) return;
    m_resizeMode = on;
    if (!m_resizeModeTimer) {
        m_resizeModeTimer = new QTimer(this);
        m_resizeModeTimer->setSingleShot(true);
        m_resizeModeTimer->setInterval(8000);
        connect(m_resizeModeTimer, &QTimer::timeout, this, [this] { setResizeMode(false); });
    }
    if (on) {
        m_resizeModeTimer->start();
    } else {
        m_resizeModeTimer->stop();
    }
    for (const auto &canvas : m_canvases)
        canvas->setHint(on ? QStringLiteral("Resize  ·  arrows  ·  Shift = bigger  ·  Esc = done") : QString());
}
void KrellisEffect::undo() { markSessionInteraction(); if (auto *canvas = current()) canvas->undoRedo(false); }
void KrellisEffect::redo() { markSessionInteraction(); if (auto *canvas = current()) canvas->undoRedo(true); }
void KrellisEffect::toggleResizeMode() { markSessionInteraction(); setResizeMode(!m_resizeMode); }
bool KrellisEffect::handleResizeKey(const KeyboardKeyEvent &e)
{
    if (e.state == KeyboardKeyState::Released) return m_inertKeys.erase(e.key) != 0;
    m_inertKeys.insert(e.key);
    m_resizeModeTimer->start();
    const bool big = e.modifiers & Qt::ShiftModifier;
    switch (e.key) {
    case Qt::Key_Escape: case Qt::Key_Return: case Qt::Key_Enter: setResizeMode(false); break;
    case Qt::Key_Left: resizeStep(QStringLiteral("left"), big ? 0.2 : 0.05); break;
    case Qt::Key_Right: resizeStep(QStringLiteral("right"), big ? 0.2 : 0.05); break;
    case Qt::Key_Up: resizeStep(QStringLiteral("up"), big ? 0.2 : 0.05); break;
    case Qt::Key_Down: resizeStep(QStringLiteral("down"), big ? 0.2 : 0.05); break;
    default: break;
    }
    return true;
}
void KrellisEffect::turnOff()
{
    // Emergency exit: unload after the shortcut handler returns, since unloading destroys this object.
    QTimer::singleShot(0, [] { effects->unloadEffect(QStringLiteral(KRELLIS_ID)); });
}
void KrellisEffect::toggleBar()
{
    markSessionInteraction();
    bool on = false;
    for (const auto &canvas : m_canvases) on = on || canvas->m_barOn;
    for (const auto &canvas : m_canvases) canvas->setBarOn(!on);
    toast(on ? QStringLiteral("Bar off") : QStringLiteral("Bar on"));
}
// A toast on the output the user is working on.
void KrellisEffect::toast(const QString &text)
{
    if (auto *canvas = current()) canvas->toast(text);
}
void KrellisEffect::applySettings(const krellis::config::Settings &settings)
{
    m_closeFocus = settings.closeFocus;
    m_floatFirst = settings.floatFirst;
    m_spawnTarget = settings.spawnTarget;
    m_peekPx = settings.peek;
    m_mapPinned = settings.mapPinned;
    m_showMap = settings.map;
    m_glide = settings.glide;
    m_landMs = settings.landMs;
    m_iconBelow = settings.iconBelow;
    m_tileMinimum = {double(settings.tileMinWidth), double(settings.tileMinHeight)};
    m_theme.setFollow(settings.followTheme);
    m_toastsOn = settings.toasts;
    m_shakeFloatOn = settings.shakeFloat;
    m_whichKey = settings.whichKey;
    m_chord.setDelay(settings.whichKeyDelayMs);
    if (!m_whichKey && m_chordTimer) hideHint();
    m_flyToOn = settings.flyTo;
    if (m_scratchpad && !settings.scratchpad) {
        // Switched off with the layer up or down: bring every scratch window back as an ordinary float and forget the marks.
        m_scratchBusy = true;
        for (EffectWindow *w : scratchWindows())
            if (w->isMinimized()) w->unminimize();
        m_scratchBusy = false;
        m_scratchMarks.clear();
    }
    m_scratchpad = settings.scratchpad;
    syncScratchShortcuts();
    m_pinOn = settings.pin;
    syncPinShortcut();
    m_tabsOn = settings.tabs;
    syncTabShortcuts();
    for (const auto &canvas : m_canvases) {
        canvas->m_closeFocus = m_closeFocus;
        canvas->m_floatFirst = m_floatFirst;
        canvas->m_spawnTarget = m_spawnTarget;
        canvas->m_peekPx = m_peekPx;
        canvas->m_mapPinned = m_mapPinned;
        canvas->m_mapOn = m_showMap;
        canvas->m_scratchpad = m_scratchpad;
        canvas->setPinOn(m_pinOn);
        canvas->setTabsOn(m_tabsOn);
        canvas->m_glide = m_glide;
        canvas->m_landMs = m_landMs;
        canvas->m_iconBelow = m_iconBelow;
        canvas->m_tileMinimum = m_tileMinimum;
        canvas->m_toasts = m_toastsOn;
        canvas->m_shakeFloat = m_shakeFloatOn;
        canvas->m_flyTo = m_flyToOn;
        canvas->m_nav.sync(); // the peek gutter changes the camera target
        canvas->updateMap();
        canvas->schedule();
    }
}
// The settings module saves krellisrc, then calls this so the change applies without a re-login.
void KrellisEffect::reloadSettings()
{
    applySettings(krellis::config::load());
    m_theme.reload(); // the harness changes kdeglobals without the D-Bus notification KConfigWatcher listens for
    reloadRules();
}
bool KrellisEffect::floatFirst() const { return m_floatFirst; }
void KrellisEffect::setFloatFirst(bool on)
{
    markSessionInteraction();
    m_floatFirst = on;
    for (const auto &canvas : m_canvases) {
        canvas->m_floatFirst = on;
        canvas->updateMap();
    }
    krellis::config::saveValue(QStringLiteral("floatFirst"), on);
    toast(on ? QStringLiteral("Floating first") : QStringLiteral("Tiling first"));
}
void KrellisEffect::toggleFloatFirst() { setFloatFirst(!m_floatFirst); }
bool KrellisEffect::mapPinned() const { return m_mapPinned; }
void KrellisEffect::setMapPinned(bool on)
{
    m_mapPinned = on;
    for (const auto &canvas : m_canvases) {
        canvas->m_mapPinned = on;
        canvas->updateMap();
    }
    krellis::config::saveValue(QStringLiteral("mapPinned"), on);
    toast(on ? QStringLiteral("Map pinned") : QStringLiteral("Map unpinned"));
}
void KrellisEffect::toggleMapPinned() { setMapPinned(!m_mapPinned); }
void KrellisEffect::toggleMap()
{
    markSessionInteraction();
    auto *canvas = current();
    if (!canvas) return;
    canvas->toggleMap();
    m_showMap = canvas->m_mapOn; // the toggle is remembered, and the other outputs follow it
    for (const auto &other : m_canvases) {
        if (other.get() == canvas || other->m_mapOn == m_showMap) continue;
        other->toggleMap();
    }
    krellis::config::saveValue(QStringLiteral("map"), m_showMap);
    canvas->toast(m_showMap ? QStringLiteral("Map on") : QStringLiteral("Map off"));
}
void KrellisEffect::saveView() { markSessionInteraction(); if (auto *canvas = current()) canvas->beginFramingName(); }
void KrellisEffect::closeActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->closeActive(); }
void KrellisEffect::back() { markSessionInteraction(); if (auto *canvas = current()) canvas->back(); }
void KrellisEffect::forward() { markSessionInteraction(); if (auto *canvas = current()) canvas->forward(); }
void KrellisEffect::zoom(double factor, double px, double py) { markSessionInteraction(); if (auto *canvas = current()) canvas->zoom(factor, px, py); }
void KrellisEffect::finishGesture() { markSessionInteraction(); if (auto *canvas = current()) canvas->finishGesture(); }

void KrellisEffect::toggleDock(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleDock(index); }
void KrellisEffect::toggleDockActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleDockActive(); }

} // namespace KWin
