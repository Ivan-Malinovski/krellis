#pragma once

#include "effect/effect.h"
#include "effect/effectwindow.h"
#include "input.h"
#include "input_event.h"
#include "effect/timeline.h"
#include "drag.h"
#include "resize.h"
#include "navigator.h"
#include "document.h"
#include "view_rules.h"
#include "session_store.h"
#include <QTimer>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace KWin {

using TRect = trellis::Rect;

class TrellisEffect;
class Window;

// Routes pointer/gesture input to the effect before windows see it.
class TrellisInput : public InputEventFilter
{
public:
    explicit TrellisInput(TrellisEffect *effect);
    bool pointerMotion(PointerMotionEvent *e) override;
    bool pointerButton(PointerButtonEvent *e) override;
    bool pointerAxis(PointerAxisEvent *e) override;
    bool keyboardKey(KeyboardKeyEvent *e) override;
    bool pinchGestureBegin(PointerPinchGestureBeginEvent *e) override;
    bool pinchGestureUpdate(PointerPinchGestureUpdateEvent *e) override;
    bool pinchGestureEnd(PointerPinchGestureEndEvent *e) override;
    bool pinchGestureCancelled(PointerPinchGestureCancelEvent *e) override;

private:
    TrellisEffect *m_effect;
};

class TrellisCanvas : public QObject, public trellis::NavigationHost, public trellis::DragHost
{
    Q_OBJECT
public:
    explicit TrellisCanvas(LogicalOutput *output);
    ~TrellisCanvas() override;

    void prePaintScreen(ScreenPrePaintData &data);
    void postPaintScreen();
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data);
    void paintWindow(const RenderTarget &rt, const RenderViewport &vp, EffectWindow *w, int mask, const Region &region, WindowPaintData &data);

    // NavigationHost
    const trellis::LayoutDocument &doc() const override { return m_doc; }
    trellis::NavMode mode() const override { return trellis::NavMode::Free; }
    trellis::Point viewport() const override;
    trellis::LayoutMetrics layoutMetrics() const override;
    void schedule() override;
    void changed() override { if (m_changed) m_changed(); }

    // DragHost (screen space is relative to the work area's top-left)
    double inset() const override;
    double tabbarHeight(const trellis::Node &) const override { return 0; }
    int tabIndexAt(const trellis::Node &panel, double) const override { return int(panel.views.size()); }
    trellis::Rect toScreen(const trellis::Rect &world) const override;
    trellis::Point fromScreen(trellis::Point screen) const override;
    trellis::Rect panelScreen(const trellis::Rect &world) const override;
    std::optional<trellis::Rect> fromScreenRect(const std::optional<trellis::Rect> &screen) const override;
    std::optional<trellis::Rect> floatWorld(const std::string &) const override { return std::nullopt; }
    std::optional<trellis::FloatingLayer> floatingLayer() const override { return trellis::FloatingLayer::Overlay; }
    trellis::Point minSize(const std::vector<std::string> &views) const override;
    bool allowed(const std::vector<std::string> &views, trellis::Region region) const override;
    bool frameOnly(const std::string &panel) const override;
    bool reducedMotion() const override { return !m_glide; }
    bool tabsAllowed() const override { return false; }
    const trellis::RectMap &lastRects() const override;
    void beginTween(const trellis::RectMap &previous, double nowMs) override;

    // Input (called by TrellisInput; return true when the event was consumed).
    bool handleButton(const PointerButtonEvent &e);
    bool handleMotion(const PointerMotionEvent &e);
    bool handleAxis(const PointerAxisEvent &e, bool titleBar = false);
    bool handleKey(const KeyboardKeyEvent &e);
    void pinch(double scaleSinceLast, QPointF at, QPointF delta);
    void endPinch();

public Q_SLOTS:
    // D-Bus control (also used by the nested test harness).
    QString dump();
    void toggleHide(int index);
    void overview();
    void toggleOverview();
    void focusIndex(int index);
    void toggleMaximize(int index);
    void stepOut();
    void stepIn();
    // Global-shortcut commands: act on the active window.
    void nextPanel();
    void previousPanel();
    void cyclePanel(int step);
    void toggleMaximizeActive();
    void toggleFloatActive();
    void toggleHideActive();
    void closeActive();
    void back();
    void forward();
    void zoom(double factor, double px, double py);
    void finishGesture();
    void toggleFloat(int index);
    void toggleDock(int index);
    void toggleDockActive();
    QString barHit(double x, double y) { return barKey({x, y}); }
    // Drive a window drag without KWin's interactive move: press on window `index` at (x, y).
    bool simDragStart(int index, double x, double y);
    void simDragMove(double x, double y);
    void simDragEnd(bool commit);

private Q_SLOTS:
    void windowAdded(KWin::EffectWindow *w);
    void windowClosed(KWin::EffectWindow *w);
    void windowActivated(KWin::EffectWindow *w);
    void minimizedChanged(KWin::EffectWindow *w);
    void userMoveStarted(KWin::EffectWindow *w);
    void userMoveFinished(KWin::EffectWindow *w);
    void resizeStepped(KWin::EffectWindow *w, const RectF &geometry);

private:
    friend class TrellisEffect;
    QPointer<LogicalOutput> m_output;
    std::function<void(EffectWindow *, QPointF)> m_transfer;
    // A KWin-driven drag whose pointer left this output: the effect moves the live drag to the canvas under it.
    std::function<bool(EffectWindow *, QPointF)> m_handoff;
    std::function<void()> m_changed;
    bool m_mapPinned = false;                // the minimap stays out instead of sliding off the edge
    std::function<void()> m_togglePin;
    std::function<void()> m_toggleMode; // the minimap pill: flips floating-first for every canvas
    QPointF m_windowPointer;
    bool m_applyingRest = false;
    bool m_steppingResize = false;
    bool m_unloading = false;
    struct ResizeBoundary { std::string split; int index; trellis::Axis axis; double start; };
    struct NativeResize {
        QPointer<EffectWindow> window;
        trellis::LayoutDocument before;
        TRect painted, camera;
        RectF native, lastNative;
        std::optional<bool> decision;
        int nativeSteps = 0;
        QPointF anchor;
        bool floating = false;
        std::vector<ResizeBoundary> boundaries;
    };
    std::optional<NativeResize> m_resize;
    void startResize(EffectWindow *window);
    void resizeAt(EffectWindow *window, QPointF pointer);
    void finishResize(bool commit);

    // The coordinator owns the baseline across canvas transfers. The flag
    // distinguishes automatic singleton suppression from user minimization.
    std::function<void(EffectWindow *, bool)> m_nativeMutation;
    std::function<std::optional<RectF>(EffectWindow *)> m_hiddenNativeRect;
    void prepareUnload();
    void detachWindow(EffectWindow *w);
    static QString appId(EffectWindow *w);
    trellis::ViewRules rulesFor(EffectWindow *w) const;
    void loadRules();
    std::map<QString, trellis::ViewRules> m_rules;
    QString m_rulesFile;
    std::map<std::string, TRect> m_rememberedFloats;
    struct Tracked {
        EffectWindow *window;
        std::string panel, view;
        std::optional<RectF> requested; // last frame applyRest asked for
        double requestedMs = 0;          // when, on the canvas clock
        std::optional<TRect> lastDrawn;  // rect paintWindow last drew it at
    };
    // True while the window's frame is where applyRest put it. A camera can place a neighbour past the
    // output edge; KWin then reports an output change that is ours, not a user transfer.
    bool placedByUs(EffectWindow *w) const;
    // Continue a drag that began on another output, with the window already tracked here.
    void adoptDrag(EffectWindow *w, QPointF pointer, const QRectF &startGeometry);
    int activeIndex() const;
    static bool manageable(EffectWindow *w);
    Tracked *trackedFor(EffectWindow *w);
    void relayout(bool animate);
    TRect panelAt(const TRect &world, const TRect &camera) const;
    // The layout rect (world space) a panel is drawn from: the live drag preview, else the document.
    const TRect *worldRect(const std::string &panel) const;
    std::optional<TRect> floatScreen(const std::string &panel) const;
    void dockAtEnd(const trellis::NodePtr &panel);
    void startDrag(EffectWindow *w, QPointF pointer);
    void moveDrag(QPointF pointer);
    void endDrag(bool commit);
    void updateSlot(double dt);
    TRect screenRect(const std::string &panel) const;
    TRect sizingCamera() const;
    TRect restRect(const std::string &panel, const TRect *world = nullptr) const;
    std::optional<TRect> visualRect(EffectWindow *window, double *opacity = nullptr) const;
    void frameChanged(EffectWindow *w, const RectF &old);
    bool awaitingLanding(const Tracked &t) const;
    bool layoutAwaitingLanding() const;
    // tweened: request the sizes of the running layout tween instead of the final ones, so clients really
    // resize through the animation and their contents are never scaled.
    void applyRest(bool stepping = false);
    void updateDock();
    TRect chipRect(EffectWindow *w) const;
    enum class FlightKind { Appear, Hide, Restore, Glide };
    struct Flight {
        FlightKind kind;
        TRect from, to; // screen rects; `to` is live-updated for Appear/Restore
        double start, duration;
        bool crossOutput = false;
        std::optional<EffectWindowVisibleRef> ref;
        std::optional<RectF> nativeRestore;
        double fromOpacity = 1;
    };
    void startFlight(EffectWindow *w, FlightKind kind, TRect from, TRect to, double opacity = 1);
    bool flightRect(EffectWindow *w, const Tracked &t, TRect &rect, double &opacity) const;

    trellis::LayoutDocument m_doc;
    trellis::RectSpring m_camera;
    trellis::Navigator m_nav;
    trellis::LayoutTween m_tween;
    trellis::Entries m_rects; // panel world rects
    std::vector<Tracked> m_windows;
    AnimationClock m_clock;
    double m_nowMs = 0;
    bool m_animating = false;
    int m_counter = 0;
    QRectF m_area;
    std::unique_ptr<class OffscreenQuickScene> m_bar;
    QRectF m_uiScreen, m_uiFrom;
    double m_areaChangedMs = -1000;
    QRectF m_screen;          // KWin panel-aware rectangle; m_area also reserves the Trellis bar
    QString m_hoverKey, m_pressKey, m_barSignature;
    bool m_mapOn = false, m_pressBar = false;
    // Dragging a square on the minimap frames the tiles it touches.
    bool m_mapDragging = false;
    QPointF m_mapAnchor;
    QRectF m_mapBand;                       // in stage pixels
    std::vector<std::string> m_mapPicks;
    bool mapSelectKey(const QString &key) const;
    void moveMapSelect(QPointF pointer);
    void finishMapSelect(bool commit);
    bool m_barOn = false; // the bar is optional; Plasma's panel already covers most of it
    bool m_naming = false, m_nameError = false;
    double barHeight() const; // space reserved for the bar, 0 while it is hidden
    void setBarOn(bool on);
    void toggleMap();
    void beginFramingName();
    void submitFramingName();
    void updateBar();
    void updateTiles(double dt);
    bool handleIconButton(const PointerButtonEvent &event);
    bool handleIconMotion(const PointerMotionEvent &event);
    struct Tile { double amount = 0, velocity = 0, target = 0; QString icon; };
    std::map<std::string, Tile> m_tiles;
    std::unique_ptr<class OffscreenQuickScene> m_tileScene;
    QStringList m_tileIds;
    std::string m_iconPressed, m_lastIconClick;
    QPointF m_iconAnchor;
    double m_lastIconClickMs = -1000;
    void updateMap(bool force = false);
    std::unique_ptr<class OffscreenQuickScene> m_map;
    QString m_mapSignature;
    bool overUi(QPointF pointer) const;
    QString barKey(QPointF pointer) const;
    void barAction(const QString &key);
    std::vector<EffectWindow *> m_dockOrder;
    enum class Drag { None, Pan, Scale, Rect };
    Drag m_drag = Drag::None;
    QPointF m_dragLast, m_dragAnchor;
    bool m_keyV = false;
    double m_pinchScale = 1;
    std::map<EffectWindow *, Flight> m_flights;
    std::map<EffectWindow *, double> m_popups;
    std::unique_ptr<trellis::DragSession> m_session;
    EffectWindow *m_dragWindow = nullptr;
    RectF m_dragStartGeometry;
    int m_simForceFloat = -1;
    bool m_floatFirst = false; // floating-first mode: new windows float, and drops tile only at screen edges and corners
    double m_landMs = 200; // glide off: duration of the move-and-scale a window plays when its real geometry changes; 0 = none
    struct Landing { TRect from; double start; };
    std::map<EffectWindow *, Landing> m_landings;
    bool m_glide = false; // our own animations (camera spring, layout tween, appear/close/popup); off = windows jump straight to their final rects
    bool m_shiftHeld = false; // from the latest pointer event; Shift floats a dragged window
    bool floatModifierHeld() const;
    bool m_dragFromKWin = false;
    trellis::RectMap m_lastRects; // what was drawn last frame; rebuilt by refreshRects()
    void refreshRects();
    std::unique_ptr<class OffscreenQuickScene> m_slot;
    double m_slotOpacity = 0;
    TRect m_lifted; // the lifted window's screen rect this frame (absolute)
    void watch(EffectWindow *w);
    // Rectangle-select gesture (Ctrl+Alt+Shift drag): a rubber band plus the framing a release would pick.
    void moveRectSelect(QPointF pointer);
    void finishRectSelect(bool commit);
    void updateMarquee(double dt);
    QString candidateLabel(const std::string &id) const;
    std::unique_ptr<class OffscreenQuickScene> m_marquee;
    TRect m_rectView;        // camera when the gesture began
    QRectF m_rectBand;       // rubber band, area-relative screen px
    std::string m_rectCandidate;
    TRect m_candTarget, m_candShown;
    QString m_candLabel;
    double m_bandOpacity = 0, m_candOpacity = 0;
    // Windows that have closed but are still painted while they fade out (kept alive by the ref).
    struct Closing {
        EffectWindowDeletedRef ref;
        TRect rect; // absolute screen rect it faded out from
        double start;
        double opacity = 1;
        EffectWindowVisibleRef visible;
    };
    std::map<EffectWindow *, Closing> m_closing;
};

// One effect-chain participant, owning an independent state machine per output.
class TrellisEffect : public Effect
{
    Q_OBJECT
public:
    TrellisEffect();
    ~TrellisEffect() override;
    bool isActive() const override { return true; }
    int requestedEffectChainPosition() const override { return 50; }
    void prePaintScreen(ScreenPrePaintData &data) override;
    void postPaintScreen() override;
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override;
    void paintWindow(const RenderTarget &rt, const RenderViewport &vp, EffectWindow *w, int mask, const Region &region, WindowPaintData &data) override;
    bool handleButton(const PointerButtonEvent &e);
    bool handleMotion(const PointerMotionEvent &e);
    bool handleAxis(const PointerAxisEvent &e);
    bool handleKey(const KeyboardKeyEvent &e);
    void pinch(double scale, QPointF at, QPointF delta);
    void endPinch();
public Q_SLOTS:
    // D-Bus control (also used by the nested test harness).
    QString dump();
    void toggleHide(int index);
    void overview();
    void toggleOverview();
    void focusIndex(int index);
    void toggleMaximize(int index);
    void stepOut();
    void stepIn();
    // Global-shortcut commands: act on the active window.
    void nextPanel();
    void previousPanel();
    void cyclePanel(int step);
    void toggleMaximizeActive();
    void toggleFloatActive();
    void toggleHideActive();
    void closeActive();
    void turnOff();
    QString version() const;
    void toggleBar();
    void toggleFloatFirst();
    void setFloatFirst(bool on);
    bool floatFirst() const;
    void setMapPinned(bool on);
    void toggleMapPinned();
    bool mapPinned() const;
    void toggleMap();
    void saveView();
    void back();
    void forward();
    void zoom(double factor, double px, double py);
    void finishGesture();
    // Synthetic input for the nested test harness (no input injector is available there).
    bool simButton(double x, double y, bool press, int modifiers);
    bool simMotion(double x, double y);
    bool simAxis(double delta, double x, double y, int modifiers);
    void simPinch(double scale, double x, double y);
    void simPinchEnd();
    void simKey(int qtKey, bool press);
    void simText(const QString &text);
    void toggleFloat(int index);
    void toggleDock(int index);
    void toggleDockActive();
    QString barHit(double x, double y);
    // Drive a window drag without KWin's interactive move: press on window `index` at (x, y).
    bool simDragStart(int index, double x, double y);
    void simDragMove(double x, double y);
    void simDragEnd(bool commit);
    // Stand in for the held float modifier: 1 held, 0 released, -1 follow the real keyboard.
    void simForceFloat(int mode);
    bool simNativeResizeStart(int index, double horizontal, double vertical);
    bool simNativeResizeMove(double dx, double dy);
    bool simNativeResizeEnd(bool commit);


    QString dumpOutput(int output);
    void selectOutput(int output);
    void moveToOutput(int window, int output);
    void reloadRules();
    bool saveSession();
    QString sessionStatus() const;
    void restoreSession();
private:
    void syncOutputs();
    void initializeSession();
    void scheduleSave();
    void sessionWindowOpened();
    void markSessionInteraction();
    bool writeSession();
    QString outputKey(LogicalOutput *output) const;
    trellis::SessionSnapshot m_savedSession;
    QTimer *m_saveTimer = nullptr, *m_restoreTimer = nullptr;
    bool m_restoreComplete = true, m_userTouched = false, m_restoring = false, m_shuttingDown = false;
    int m_sessionMatches = 0, m_sessionAmbiguous = 0, m_pendingUnique = 0;
    QString m_sessionFile, m_sessionError;
    void watchOutput(EffectWindow *window);
    void rememberNative(EffectWindow *window, bool automaticMinimize, bool mutation = true);
    void restoreNative();
    struct NativeState {
        QPointer<Window> window;
        RectF geometry;
        QPointer<LogicalOutput> output;
        bool keepAbove, keepBelow, minimized;
        bool automaticMinimize = false;
        bool mutated = false;
    };
    std::map<Window *, NativeState> m_nativeStates;
    struct SurfaceHit {
        EffectWindow *window;
        TrellisCanvas *canvas;
        TRect shown;
        QPointF mapped;
        bool decoration, inert;
    };
    std::optional<SurfaceHit> surfaceHit(QPointF point) const;
    bool surfaceMotion(const PointerMotionEvent &event);
    bool surfaceButton(const PointerButtonEvent &event);
    bool surfaceAxis(const PointerAxisEvent &event);
    void leaveSurface();
    void focusSurface(const SurfaceHit &hit, QPointF position);
    void focusDecoration(const SurfaceHit &hit, QPointF position);
    void leaveDecoration();
    QPointer<EffectWindow> m_decorWindow;
    QPointer<EffectWindow> m_clientGrab;
    std::set<quint32> m_clientButtons, m_dismissedButtons;
    std::set<int> m_inertKeys;
    bool m_grabDecoration = false;
    QPointF m_decorPosition;
    void registerShortcuts();
    void transfer(EffectWindow *window, TrellisCanvas *target);
    TrellisCanvas *canvasAt(QPointF point) const;
    TrellisCanvas *owner(EffectWindow *window) const;
    TrellisCanvas *current() const;
    std::vector<std::unique_ptr<TrellisCanvas>> m_canvases;
    std::unique_ptr<TrellisInput> m_input;
    TrellisCanvas *m_pointerCanvas = nullptr, *m_pinchCanvas = nullptr;
    LogicalOutput *m_selectedOutput = nullptr;
    bool m_floatFirst = false; // loaded from trellisrc; mirrored into every canvas
    bool m_mapPinned = false;  // likewise
    double m_landMs = 200; // loaded from trellisrc ([General] landMs); mirrored into every canvas
    bool m_glide = false; // loaded from trellisrc ([General] glide, default off); mirrored into every canvas
};

} // namespace KWin
