#include "trellis_effect.h"
#include "effect/effecthandler.h"
#include "core/output.h"
#include "core/renderviewport.h"
#include "window.h"
#include "wayland_server.h"
#include "wayland/seat.h"
#include <QDBusConnection>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

namespace KWin {

// Compile stamp so a running compositor can be told apart from the installed file (see tools/live.sh).
static const char kBuildStamp[] = "build " __DATE__ " " __TIME__; // ASCII so tools can grep the .so
QString TrellisEffect::version() const { return QString::fromLatin1(kBuildStamp); }

TrellisEffect::TrellisEffect()
{
    const QSettings config(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/trellisrc"), QSettings::IniFormat);
    // Qt's INI reader files [General] keys under their bare names, so "General/key" never matches on read.
    m_floatFirst = config.value(QStringLiteral("floatFirst"), false).toBool();
    m_placeAtPointer = config.value(QStringLiteral("placeAtPointer"), true).toBool();
    m_peekPx = std::max(0.0, config.value(QStringLiteral("peek"), 40).toDouble());
    m_mapPinned = config.value(QStringLiteral("mapPinned"), false).toBool();
    m_glide = config.value(QStringLiteral("glide"), false).toBool();
    m_landMs = std::max(0.0, config.value(QStringLiteral("landMs"), 200).toDouble());
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
        if (TrellisCanvas::manageable(w)) rememberNative(w, false, false);
        for (const auto &canvas : m_canvases)
            if (canvas->m_output == w->screen()) {
                canvas->windowAdded(w);
                sessionWindowOpened();
                break;
            }
    });
    connect(effects, &EffectsHandler::windowActivated, this, [this](EffectWindow *w) {
        if (owner(w)) m_selectedOutput = nullptr;
    });
    for (auto *window : effects->stackingOrder()) {
        watchOutput(window);
        if (TrellisCanvas::manageable(window)) rememberNative(window, false, false);
        for (const auto &canvas : m_canvases)
            if (canvas->m_output == window->screen())
                canvas->windowAdded(window);
    }
    if (!effects->stackingOrder().isEmpty()) sessionWindowOpened();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TrellisEffect::syncOutputs);
    timer->start(100);
    registerShortcuts();
    m_input = std::make_unique<TrellisInput>(this);
    input()->installInputEventFilter(m_input.get());
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Trellis"), this,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllInvokables);
}

TrellisEffect::~TrellisEffect()
{
    m_shuttingDown = true;
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
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/Trellis"));
}

void TrellisEffect::rememberNative(EffectWindow *window, bool automaticMinimize, bool mutation)
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

void TrellisEffect::restoreNative()
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
        // Trellis. Only an automatic singleton suppression is undone.
        if (state.automaticMinimize && !state.minimized && window && !window->isDeleted()) window->setMinimized(false);
    }
    m_nativeStates.clear();
}

// Observe explicit KWin moves between outputs, excluding our camera-driven geometry.
void TrellisEffect::watchOutput(EffectWindow *window)
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

void TrellisEffect::syncOutputs()
{
    if (m_shuttingDown) return;
    const auto outputs = effects->screens();
    for (auto *output : outputs) {
        if (std::any_of(m_canvases.begin(), m_canvases.end(), [output](const auto &c) { return c->m_output == output; }))
            continue;
        auto canvas = std::make_unique<TrellisCanvas>(output);
        canvas->m_floatFirst = m_floatFirst;
        canvas->m_placeAtPointer = m_placeAtPointer;
        canvas->m_peekPx = m_peekPx;
        canvas->m_glide = m_glide;
        canvas->m_landMs = m_landMs;
        canvas->m_nativeMutation = [this](EffectWindow *window, bool automatic) { rememberNative(window, automatic); };
        canvas->m_hiddenNativeRect = [this](EffectWindow *window) -> std::optional<RectF> {
            if (auto it = m_nativeStates.find(window->window()); it != m_nativeStates.end()) return it->second.geometry;
            return std::nullopt;
        };
        canvas->m_changed = [this] { scheduleSave(); };
        canvas->m_toggleMode = [this] { toggleFloatFirst(); };
        canvas->m_mapPinned = m_mapPinned;
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
        TrellisCanvas *target = nullptr;
        for (const auto &c : m_canvases)
            if (outputs.contains(c->m_output)) { target = c.get(); break; }
        if (!target) { ++it; continue; } // Preserve documents until an output is available.
        const auto windows = removed->m_windows;
        removed->m_session.reset();
        removed->m_dragWindow = nullptr;
        for (const auto &t : windows)
            transfer(t.window, target);
        it = m_canvases.erase(it);
        effects->addRepaintFull();
    }
}

TrellisCanvas *TrellisEffect::owner(EffectWindow *w) const
{
    for (const auto &canvas : m_canvases)
        if (canvas->trackedFor(w) || canvas->m_closing.count(w))
            return canvas.get();
    if (w && w->isPopupWindow()) {
        auto *parent = w->transientFor();
        if (parent && parent != w) return owner(parent);
    }
    return nullptr;
}

TrellisCanvas *TrellisEffect::canvasAt(QPointF point) const
{
    for (const auto &canvas : m_canvases)
        if (effects->screens().contains(canvas->m_output) && QRectF(QRect(canvas->m_output->geometry())).contains(point))
            return canvas.get();
    return nullptr;
}

TrellisCanvas *TrellisEffect::current() const
{
    for (const auto &canvas : m_canvases)
        if (canvas->m_output == m_selectedOutput)
            return canvas.get();
    if (auto *canvas = owner(effects->activeWindow())) return canvas;
    if (auto *canvas = canvasAt(effects->cursorPos())) return canvas;
    return m_canvases.empty() ? nullptr : m_canvases.front().get();
}

void TrellisCanvas::detachWindow(EffectWindow *w)
{
    if (auto *tracked = trackedFor(w)) {
        if (m_resize) finishResize(false);
        const std::string view = tracked->view;
        if (m_dragWindow == w) {
            m_session.reset();
            m_dragWindow = nullptr;
            m_dragFromKWin = false;
        }
        m_flights.erase(w);
        m_doc = trellis::closeView(m_doc, view);
        std::erase_if(m_windows, [w](const auto &t) { return t.window == w; });
        relayout(true);
        updateDock();
    }
}

void TrellisEffect::transfer(EffectWindow *w, TrellisCanvas *target)
{
    auto *source = owner(w);
    if (!source || source == target || !source->trackedFor(w)) return;
    if (source->m_resize) source->finishResize(false);
    if (target->m_resize) target->finishResize(false);
    auto *tracked = source->trackedFor(w);
    TRect from = source->screenRect(tracked->panel);
    double opacity = 1;
    source->flightRect(w, *tracked, from, opacity);
    const bool hidden = w->isMinimized();
    source->detachWindow(w);
    target->windowAdded(w);
    if (hidden) target->minimizedChanged(w);
    else {
        target->startFlight(w, TrellisCanvas::FlightKind::Glide, from, {}, opacity);
        target->m_flights.at(w).crossOutput = true;
    }
    target->updateDock();
}

void TrellisEffect::prePaintScreen(ScreenPrePaintData &data)
{
    // Surface damage arrives in native coordinates, while every managed window
    // can be painted elsewhere. The simple path cannot transform that damage.
    data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    data.paint = Region::infinite();
    for (const auto &canvas : m_canvases) canvas->prePaintScreen(data);
    effects->prePaintScreen(data);
}
void TrellisEffect::postPaintScreen()
{
    for (const auto &canvas : m_canvases) canvas->postPaintScreen();
    effects->postPaintScreen();
}
void TrellisEffect::prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data)
{
    if (auto *canvas = owner(w)) canvas->prePaintWindow(view, w, data);
    effects->prePaintWindow(view, w, data);
}
void TrellisEffect::paintWindow(const RenderTarget &rt, const RenderViewport &vp, EffectWindow *w, int mask, const Region &region, WindowPaintData &data)
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

bool TrellisEffect::handleButton(const PointerButtonEvent &e)
{
    if (e.state == PointerButtonState::Pressed) markSessionInteraction();
    if (auto *active = effects->activeWindow(); active && (active->isUserMove() || active->isUserResize())) {
        // A decoration press was delivered by us, so its release belongs to the
        // same painted window even when KWin's native hit is now elsewhere.
        if (m_clientGrab && m_grabDecoration && e.state == PointerButtonState::Released)
            return surfaceButton(e);
        return false; // KWin's interactive-move filter follows Effects in the chain.
    }
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
bool TrellisEffect::handleMotion(const PointerMotionEvent &e)
{
    for (const auto &canvas : m_canvases) canvas->m_shiftHeld = e.modifiers & Qt::ShiftModifier;
    if (auto *active = effects->activeWindow(); active && (active->isUserMove() || active->isUserResize())) {
        if (active->isUserResize())
            if (auto *canvas = owner(active)) canvas->resizeAt(active, e.position);
        return false; // KWin still owns the native interactive grab.
    }
    if (m_pointerCanvas) {
        leaveSurface();
        return m_pointerCanvas->handleMotion(e);
    }
    if (m_clientGrab) return surfaceMotion(e);
    bool handled = false;
    for (const auto &canvas : m_canvases) handled = canvas->handleMotion(e) || handled;
    return handled || surfaceMotion(e);
}
bool TrellisEffect::handleAxis(const PointerAxisEvent &e)
{
    if (auto *active = effects->activeWindow(); active && (active->isUserMove() || active->isUserResize()))
        return false; // KWin's interactive-move filter follows Effects in the chain.
    auto *canvas = canvasAt(e.position);
    // Scrolling over a server-side title bar zooms without any key held.
    bool titleBar = false;
    if (canvas && !m_clientGrab) {
        if (const auto hit = surfaceHit(e.position); hit && hit->decoration && !hit->inert && hit->canvas == canvas
            && hit->window->window())
            titleBar = hit->mapped.y() < hit->window->window()->clientGeometry().top();
    }
    return (canvas && canvas->handleAxis(e, titleBar)) || surfaceAxis(e);
}
bool TrellisEffect::handleKey(const KeyboardKeyEvent &e)
{
    if (auto *active = effects->activeWindow(); active && (active->isUserMove() || active->isUserResize())) {
        if (e.key == Qt::Key_Escape && e.state == KeyboardKeyState::Pressed)
            if (auto *canvas = owner(active); canvas && canvas->m_resize) canvas->m_resize->decision = false;
        return false; // Let KWin handle Escape, Return and keyboard resize steps.
    }
    if (e.state == KeyboardKeyState::Pressed) markSessionInteraction();
    TrellisCanvas *editing = nullptr;
    for (const auto &canvas : m_canvases) if (canvas->m_naming) { editing = canvas.get(); break; }
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
        busy = busy || canvas->m_nav.gestureActive() || canvas->m_drag != TrellisCanvas::Drag::None || bool(canvas->m_session);
    }
    if (e.state == KeyboardKeyState::Released && m_inertKeys.erase(e.key)) return true;
    if (busy && e.state != KeyboardKeyState::Released) {
        m_inertKeys.insert(e.key);
        return true;
    }
    return handled;
}
void TrellisEffect::pinch(double scale, QPointF at, QPointF delta)
{
    if (!m_pinchCanvas) m_pinchCanvas = canvasAt(at);
    leaveSurface();
    if (m_pinchCanvas) m_pinchCanvas->pinch(scale, at, delta);
}
void TrellisEffect::endPinch()
{
    if (m_pinchCanvas) m_pinchCanvas->endPinch();
    m_pinchCanvas = nullptr;
}
QString TrellisEffect::barHit(double x, double y)
{
    auto *canvas = canvasAt({x, y});
    return canvas ? canvas->barKey({x, y}) : QString();
}
QString TrellisEffect::dump()
{
    if (m_canvases.size() == 1) return m_canvases.front()->dump();
    QStringList lines;
    for (size_t i = 0; i < m_canvases.size(); ++i)
        lines << QStringLiteral("OUTPUT %1 %2\n%3").arg(i).arg(m_canvases[i]->m_output->name(), m_canvases[i]->dump());
    return lines.join(QLatin1Char('\n'));
}
QString TrellisEffect::dumpOutput(int i)
{
    return i >= 0 && i < int(m_canvases.size()) ? m_canvases[size_t(i)]->dump() : QString();
}
void TrellisEffect::selectOutput(int i)
{
    m_selectedOutput = i >= 0 && i < int(m_canvases.size()) ? m_canvases[size_t(i)]->m_output : nullptr;
}
void TrellisEffect::moveToOutput(int window, int output)
{
    markSessionInteraction();
    auto *canvas = current();
    if (!canvas || window < 0 || window >= int(canvas->m_windows.size()) || output < 0 || output >= int(m_canvases.size())) return;
    transfer(canvas->m_windows[size_t(window)].window, m_canvases[size_t(output)].get());
}
bool TrellisEffect::simDragStart(int i, double x, double y)
{
    auto *canvas = current();
    if (!canvas || !canvas->simDragStart(i, x, y)) return false;
    m_pointerCanvas = canvas;
    return true;
}
void TrellisEffect::simDragMove(double x, double y)
{
    if (m_pointerCanvas) m_pointerCanvas->simDragMove(x, y);
}
void TrellisEffect::simForceFloat(int mode)
{
    for (const auto &canvas : m_canvases) canvas->m_simForceFloat = mode;
}
void TrellisEffect::simDragEnd(bool commit)
{
    auto *canvas = m_pointerCanvas;
    m_pointerCanvas = nullptr;
    if (canvas) canvas->simDragEnd(commit);
}
void TrellisEffect::overview() { markSessionInteraction(); if (auto *canvas = current()) canvas->overview(); }
void TrellisEffect::toggleOverview() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleOverview(); }
void TrellisEffect::focusIndex(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->focusIndex(index); }
void TrellisEffect::toggleMaximize(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleMaximize(index); }
void TrellisEffect::toggleHide(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleHide(index); }
void TrellisEffect::toggleFloat(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleFloat(index); }
void TrellisEffect::stepOut() { markSessionInteraction(); if (auto *canvas = current()) canvas->stepOut(); }
void TrellisEffect::stepIn() { markSessionInteraction(); if (auto *canvas = current()) canvas->stepIn(); }
void TrellisEffect::nextPanel() { markSessionInteraction(); if (auto *canvas = current()) canvas->nextPanel(); }
void TrellisEffect::previousPanel() { markSessionInteraction(); if (auto *canvas = current()) canvas->previousPanel(); }
void TrellisEffect::cyclePanel(int step) { markSessionInteraction(); if (auto *canvas = current()) canvas->cyclePanel(step); }
void TrellisEffect::toggleMaximizeActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleMaximizeActive(); }
void TrellisEffect::toggleFloatActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleFloatActive(); }
void TrellisEffect::toggleHideActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleHideActive(); }
void TrellisEffect::turnOff()
{
    // Emergency exit: unload after the shortcut handler returns, since unloading destroys this object.
    QTimer::singleShot(0, [] { effects->unloadEffect(QStringLiteral(TRELLIS_ID)); });
}
void TrellisEffect::toggleBar()
{
    markSessionInteraction();
    bool on = false;
    for (const auto &canvas : m_canvases) on = on || canvas->m_barOn;
    for (const auto &canvas : m_canvases) canvas->setBarOn(!on);
}
static QString configPath() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/trellisrc"); }
bool TrellisEffect::floatFirst() const { return m_floatFirst; }
void TrellisEffect::setFloatFirst(bool on)
{
    markSessionInteraction();
    m_floatFirst = on;
    for (const auto &canvas : m_canvases) {
        canvas->m_floatFirst = on;
        canvas->updateMap();
    }
    QSettings config(configPath(), QSettings::IniFormat);
    config.setValue(QStringLiteral("General/floatFirst"), on);
    config.sync();
}
void TrellisEffect::toggleFloatFirst() { setFloatFirst(!m_floatFirst); }
bool TrellisEffect::mapPinned() const { return m_mapPinned; }
void TrellisEffect::setMapPinned(bool on)
{
    m_mapPinned = on;
    for (const auto &canvas : m_canvases) {
        canvas->m_mapPinned = on;
        canvas->updateMap();
    }
    QSettings config(configPath(), QSettings::IniFormat);
    config.setValue(QStringLiteral("General/mapPinned"), on);
    config.sync();
}
void TrellisEffect::toggleMapPinned() { setMapPinned(!m_mapPinned); }
void TrellisEffect::toggleMap() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleMap(); }
void TrellisEffect::saveView() { markSessionInteraction(); if (auto *canvas = current()) canvas->beginFramingName(); }
void TrellisEffect::closeActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->closeActive(); }
void TrellisEffect::back() { markSessionInteraction(); if (auto *canvas = current()) canvas->back(); }
void TrellisEffect::forward() { markSessionInteraction(); if (auto *canvas = current()) canvas->forward(); }
void TrellisEffect::zoom(double factor, double px, double py) { markSessionInteraction(); if (auto *canvas = current()) canvas->zoom(factor, px, py); }
void TrellisEffect::finishGesture() { markSessionInteraction(); if (auto *canvas = current()) canvas->finishGesture(); }

void TrellisEffect::toggleDock(int index) { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleDock(index); }
void TrellisEffect::toggleDockActive() { markSessionInteraction(); if (auto *canvas = current()) canvas->toggleDockActive(); }

} // namespace KWin
