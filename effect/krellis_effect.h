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
#include "krellis_config.h"
#include "keynav.h"
#include "theme.h"
#include "chord_hint.h"
#include "history.h"
#include "reconcile.h"
#include <QTimer>
#include <QVariantList>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace KWin {

using TRect = krellis::Rect;

class KrellisEffect;
class Window;

// Routes pointer/gesture input to the effect before windows see it.
class KrellisInput : public InputEventFilter
{
public:
    explicit KrellisInput(KrellisEffect *effect);
    bool pointerMotion(PointerMotionEvent *e) override;
    bool pointerButton(PointerButtonEvent *e) override;
    bool pointerAxis(PointerAxisEvent *e) override;
    bool keyboardKey(KeyboardKeyEvent *e) override;
    bool pinchGestureBegin(PointerPinchGestureBeginEvent *e) override;
    bool pinchGestureUpdate(PointerPinchGestureUpdateEvent *e) override;
    bool pinchGestureEnd(PointerPinchGestureEndEvent *e) override;
    bool pinchGestureCancelled(PointerPinchGestureCancelEvent *e) override;

private:
    KrellisEffect *m_effect;
};

class KrellisCanvas : public QObject, public krellis::NavigationHost, public krellis::DragHost
{
    Q_OBJECT
public:
    explicit KrellisCanvas(LogicalOutput *output);
    ~KrellisCanvas() override;

    void prePaintScreen(ScreenPrePaintData &data);
    void postPaintScreen();
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data);
    void paintWindow(const RenderTarget &rt, const RenderViewport &vp, EffectWindow *w, int mask, const Region &region, WindowPaintData &data);

    // NavigationHost
    const krellis::LayoutDocument &doc() const override { return m_doc; }
    krellis::NavMode mode() const override { return krellis::NavMode::Free; }
    krellis::Point viewport() const override;
    krellis::FloatMargin floatMargin() const override { return m_doc.floatMargin; }
    double peekPadding() const override { return m_peekPx; }
    krellis::LayoutMetrics layoutMetrics() const override;
    void schedule() override;
    void changed() override { if (m_changed) m_changed(); }

    // DragHost (screen space is relative to the work area's top-left)
    double inset() const override;
    double tabbarHeight(const krellis::Node &panel) const override { return tabInset(panel.id); }
    double tabDropHeight(const krellis::Node &panel) const override; // the title-bar band of any tile, tabbed or not
    bool bodyTabs() const override { return false; }                 // a window is tabbed by dropping it on a title bar
    int tabIndexAt(const krellis::Node &panel, double x) const override;
    krellis::Rect toScreen(const krellis::Rect &world) const override;
    krellis::Point fromScreen(krellis::Point screen) const override;
    krellis::Rect panelScreen(const krellis::Rect &world) const override;
    std::optional<krellis::Rect> fromScreenRect(const std::optional<krellis::Rect> &screen) const override;
    std::optional<krellis::Rect> floatWorld(const std::string &) const override { return std::nullopt; }
    std::optional<krellis::FloatingLayer> floatingLayer() const override { return krellis::FloatingLayer::Overlay; }
    krellis::Point minSize(const std::vector<std::string> &views) const override;
    krellis::Point clientMinimum(EffectWindow *w) const; // the rule's minimum, raised to what the client itself will not go under
    bool allowed(const std::vector<std::string> &views, krellis::Region region) const override;
    bool frameOnly(const std::string &panel) const override;
    bool reducedMotion() const override { return !m_glide; }
    bool flyTo() const override { return m_flyTo; }
    double clockMs() const override { return realMs(); }
    bool tabsAllowed() const override { return m_tabsOn; }
    const krellis::RectMap &lastRects() const override;
    void beginTween(const krellis::RectMap &previous, double nowMs) override;

    // Input (called by KrellisInput; return true when the event was consumed).
    bool handleButton(const PointerButtonEvent &e);
    bool handleMotion(const PointerMotionEvent &e);
    bool handleAxis(const PointerAxisEvent &e, bool titleBar = false);
    bool handleKey(const KeyboardKeyEvent &e);
    void pinch(double scaleSinceLast, QPointF at, QPointF delta);
    void endPinch();

    // The quick-jump palette. The coordinator builds the candidates (it sees every output); this canvas shows them,
    // ranks them against what is typed and runs the chosen one's `go`.
    struct PaletteItem {
        QString title, app, detail;
        bool view = false; // a saved view rather than a window
        std::function<void()> go;
    };
    void openPalette(std::vector<PaletteItem> items);
    void closePalette();
    bool paletteOpen() const { return m_paletteOn; }
    bool capturesKeys() const { return m_naming || m_paletteOn; } // a text field has the keyboard
    QString paletteHit(QPointF p) const; // "row:N", "card", or empty outside the card
    void paletteHover(int row);          // select a visible row
    void paletteScroll(double delta, bool wheel);
    void jumpTo(EffectWindow *w); // bring a tracked window into view and focus it
    EffectWindow *pinAt(QPointF p) const; // the pinned window whose thumbnail is under p, if any
    bool isPinned(const std::string &panel) const;
    void togglePinActive();
    void setPinOn(bool on); // off drops every pin, thumbnails glide back to their tiles

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
    void stashActive(); // slide the active window into the nearest edge as a drawer; again docks it back
    void flipActive(); // turn the active window and its neighbour from stacked to side-by-side, or back
    void toggleHideActive();
    void closeActive();
    void back();
    void forward();
    void zoom(double factor, double px, double py);
    void finishGesture();
    void toggleFloat(int index);
    void toggleDock(int index);
    void toggleDockActive();
    // Keyboard navigation (directions are screen sides): act on the active window.
    void focusDirection(krellis::Edge dir);
    // False when the active tile is docked but cannot move that way (it is at the edge): the caller may hand it on.
    bool moveDirection(krellis::Edge dir);
    // The window just arrived from the output on the `entry` side: dock it along that edge of the layout.
    void enterFrom(EffectWindow *w, krellis::Edge entry);
    bool resizeStep(krellis::Edge dir, double fraction);
    bool undoRedo(bool redo); // false when there is nothing to do or an edit is in progress (both say so in a toast)
    void clearHistory() { m_history.clear(); }
    void toast(const QString &text); // no-op with toasts off; a repeat of the visible text only restarts its timer
    void showSheet(const QVariantList &groups); // the which-key sheet; fades in
    void hideSheet();
    void setHint(const QString &hint) { if (hint != m_hint) { m_hint = hint; updateBar(); } }
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
    friend class KrellisEffect;
    QPointer<LogicalOutput> m_output;
    std::function<void(EffectWindow *, QPointF)> m_transfer;
    // A KWin-driven drag whose pointer left this output: the effect moves the live drag to the canvas under it.
    std::function<bool(EffectWindow *, QPointF)> m_handoff;
    std::function<void()> m_changed;
    bool m_mapPinned = false;                // the minimap stays out instead of sliding off the edge
    std::function<void()> m_togglePin;
    // Scratchpad: the effect owns the marks (they follow a window across outputs). Both may be unset.
    std::function<bool(EffectWindow *)> m_markedScratch;
    std::function<void(const QString &)> m_scratchAction; // "toggle" | "mark", from the minimap
    std::function<QVariantMap()> m_scratchState;           // {count, shown, active, canMark} across all outputs
    std::function<void()> m_toggleMode; // the minimap pill: flips floating-first for every canvas
    QPointF m_windowPointer;
    bool m_applyingRest = false;
    bool m_steppingResize = false;
    bool m_unloading = false;
    bool m_toasts = true;
    bool m_flyTo = false; // long camera jumps zoom out, pan and zoom in (needs glide)
    struct ResizeBoundary { std::string split; int index; krellis::Axis axis; double start; };
    struct NativeResize {
        QPointer<EffectWindow> window;
        krellis::LayoutDocument before;
        TRect painted, camera;
        RectF native, lastNative;
        std::optional<bool> decision;
        int nativeSteps = 0;
        QPointF anchor;
        bool floating = false;
        std::vector<ResizeBoundary> boundaries;
        std::optional<krellis::Edge> stash; // an edge tile squeezed below the collapse width: the edge it slides out of
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
    krellis::ViewRules rulesFor(EffectWindow *w) const;
    void loadRules();
    std::map<QString, krellis::ViewRules> m_rules;
    QString m_rulesFile;
    std::map<std::string, TRect> m_rememberedFloats;
    // Drawers: an edge tile resized to a sliver. It becomes an overlay float (its revealed rect) that rests mostly
    // past the screen edge, leaving `m_drawerStripPx` visible, and slides out while the pointer is over it.
    struct Drawer { krellis::Edge edge; bool open = false; int seq = 0; bool pinned = false; }; // seq: stash order, which is the order along the edge
    std::map<std::string, Drawer> m_drawers;
    double m_drawerStripPx = 12;     // sliver left in view while closed
    double m_drawerCollapsePx = 40;  // an edge tile squeezed thinner than this collapses
    double m_drawerTabPx = 28, m_drawerTabLen = 150;  // the labelled tab a closed drawer wears: thickness, longest length
    int m_drawerSeq = 0;
    std::unique_ptr<class OffscreenQuickScene> m_drawerTabs;
    QString m_drawerTabsSignature;
    std::optional<TRect> drawerTab(const std::string &panel) const; // a closed drawer's tab on screen
    void updateDrawerTabs();
    void setDrawerOpen(const std::string &panel, bool open, bool pinned = false); // slide a drawer out or back; pinned stays out until the pointer has visited it
    void slideOutDrawer(const std::string &panel); // brings a stashed window into view, closing any other drawer first; a no-op for anything else
    bool drawerEdgeFree(krellis::Edge edge) const; // false where another display joins this one: a drawer there would open onto it
    void stashPanel(const std::string &panel, krellis::Edge edge, const TRect &from);
    TRect drawerMapRect(const std::string &panel, const krellis::Rect &open) const; // what the minimap draws of a float
    std::optional<TRect> drawerSlice(const std::string &panel) const; // what a closed drawer shows of its edge: its share of it
    void updateDrawerHover(const PointerMotionEvent &event);
    void forgetDrawer(const std::string &panel); // also drops the paint elevation an open one has
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
    static bool leftAlone(EffectWindow *w);
    // A scratch window is a floating overlay that the scratchpad shortcut shows and hides: marked at run time or by a rule.
    bool scratchWindow(EffectWindow *w) const;
    // Turn the docked window into a centred overlay float. False when it can't (nothing left to tile, or not docked).
    bool m_pinOn = false; // [General] pin, mirrored from the effect
    bool m_tabsOn = false; // [General] tabs, mirrored from the effect
    bool m_scratchpad = false; // [General] scratchpad, mirrored from the effect; off, no window counts as scratch
    krellis::Theme *m_theme = nullptr; // owned by the effect; the tokens every overlay scene binds to
    // An overlay scene with the theme handed in as its `theme` property; null (and logged) when the QML fails to load.
    std::unique_ptr<OffscreenQuickScene> makeScene(const QString &file);
    void themeChanged(); // the colours moved: repaint every overlay
    bool floatForScratch(EffectWindow *w, int slot = 0); // slot offsets the default rect so marked windows cascade
    // The scratch layer is up while any scratch window is shown on this output; the rest of the canvas dims under it.
    bool layerShown() const;
    double m_layerDim = 0, m_layerTarget = 0;
    Tracked *trackedFor(EffectWindow *w);
    Tracked *trackedForPanel(const std::string &panel);
    // Tabbed tiles (opt-in, `tabs`). A panel can hold several windows; one is shown under a tab strip and the rest are
    // stowed: sized like it, kept out of paint and input. m_windows holds only the shown window of each panel, so the
    // rest of the canvas sees one window per panel as before.
    void setTabsOn(bool on); // off splits every tabbed panel back into one tile per window
    bool tabsOn() const { return m_tabsOn; }
    Tracked *stowedFor(EffectWindow *w);
    Tracked *trackedOfView(const std::string &view); // shown or stowed
    bool behindTab(EffectWindow *w) const;            // stowed, or a popup of a stowed window: neither painted nor hit
    const Tracked *stowedFor(const EffectWindow *w) const;
    double tabInset(const std::string &panel) const; // height of the strip a panel wears, 0 when it has none
    TRect belowTabs(const std::string &panel, TRect rect) const; // the part of a panel's rect under its strip
    void syncTabs();                                  // re-seat every window to the panel and tab the document says
    void updateTabStrips();
    struct TabHit { std::string panel, view; };
    std::optional<TabHit> tabAt(QPointF p) const;     // the tab under p, if p is on a strip
    bool tabButton(const PointerButtonEvent &e);
    bool tabMotion(QPointF p);
    void selectTab(const std::string &view, bool activate = true);
    void cycleTab(int step);                          // the active panel's next or previous tab
    void closeTab(const std::string &view);
    bool groupWithNeighbour(krellis::Edge dir);       // tab the active window into the tile that way; false when there is none
    void splitTabOut();                               // the active window leaves its tabs for a tile of its own

    std::vector<Tracked> m_stowed;  // windows behind another tab of their panel
    std::set<std::string> m_tabbed; // panels wearing a strip: tabs on and two or more windows
    std::unique_ptr<class OffscreenQuickScene> m_tabStrips;
    QString m_tabStripsSignature;
    struct TabPress { std::string panel, view; QPointF at; bool bar = false; }; // bar: pressed on the bare strip, so it drags the tab on show and a click does nothing
    std::optional<TabPress> m_tabPress; // a button is down on a tab and has not become a drag yet
    std::string m_dragTabView;          // the tab being torn out: its strip entry is gone while it is in hand
    struct Strip { std::string panel; TRect rect; double tabW; std::vector<std::string> views, titles; std::string selected; };
    std::vector<Strip> m_strips;        // the strips on show, absolute screen coordinates
    void computeStrips();
    static constexpr double TAB_H = 28, TAB_MAX_W = 220;
    void relayout(bool animate);
    TRect panelAt(const TRect &world, const TRect &camera) const;
    // The layout rect (world space) a panel is drawn from: the live drag preview, else the document.
    const TRect *worldRect(const std::string &panel) const;
    std::optional<TRect> floatScreen(const std::string &panel) const;
    void dockAtEnd(const krellis::NodePtr &panel);
    void dockAtSpawn(const krellis::NodePtr &panel); // by m_spawnTarget
    void dockAtPointer(const krellis::NodePtr &panel);
    void dockAtFocused(const krellis::NodePtr &panel);
    void startDrag(EffectWindow *w, QPointF pointer, const std::string &viewId = std::string());
    void moveDrag(QPointF pointer);
    void endDrag(bool commit);
    void updateSlot(double dt);
    bool fillVacancyFor(const krellis::NodePtr &panel, const std::optional<QPointF> &cursor);
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

    krellis::LayoutDocument m_doc;
    // Undo/redo of layout edits the user made. Window opens and closes, minimizing and scratchpad show/hide are not
    // edits; undo restores the arrangement and keeps what the compositor owns (see krellis::reconcile).
    krellis::History m_history;
    int m_editDepth = 0;
    static double realMs();
    void commitEdit(const char *label, const krellis::LayoutDocument &before, const krellis::LayoutDocument &next, const std::string &key = {});
    // Records the document as it was on entry, if it differs on exit. Nested scopes leave the recording to the outermost.
    class EditScope
    {
    public:
        EditScope(KrellisCanvas &canvas, const char *label, std::string key = {})
            : m_canvas(canvas), m_label(label), m_key(std::move(key)), m_outer(canvas.m_editDepth++ == 0), m_before(canvas.m_doc) {}
        ~EditScope()
        {
            --m_canvas.m_editDepth;
            if (m_outer) m_canvas.commitEdit(m_label, m_before, m_canvas.m_doc, m_key);
        }
        EditScope(const EditScope &) = delete;
        EditScope &operator=(const EditScope &) = delete;
    private:
        KrellisCanvas &m_canvas;
        const char *m_label;
        std::string m_key;
        bool m_outer;
        krellis::LayoutDocument m_before;
    };
    krellis::RectSpring m_camera;
    krellis::Navigator m_nav;
    krellis::LayoutTween m_tween;
    krellis::Entries m_rects; // panel world rects
    std::vector<Tracked> m_windows;
    AnimationClock m_clock;
    double m_nowMs = 0;
    bool m_animating = false;
    int m_counter = 0;
    QRectF m_area;
    std::unique_ptr<class OffscreenQuickScene> m_bar;
    QRectF m_uiScreen, m_uiFrom;
    double m_areaChangedMs = -1000;
    QRectF m_screen;          // KWin panel-aware rectangle; m_area also reserves the Krellis bar
    QString m_hoverKey, m_pressKey, m_barSignature;
    bool m_mapOn = false, m_pressBar = false;
    bool m_swallowMiddle = false; // a middle press flipped a seam: its release is ours too
    bool seamAt(QPointF pos, std::string *split, int *index) const;
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
    QString m_hint; // modal key hint shown on the bar (resize mode)
    double barHeight() const; // space reserved for the bar, 0 while it is hidden
    void setBarOn(bool on);
    void toggleMap();
    void beginFramingName();
    void submitFramingName();
    void updateBar();
    void updateTiles(double dt);
    bool handleIconButton(const PointerButtonEvent &event);
    bool handleIconMotion(const PointerMotionEvent &event);
    /// A tile outside the framing whose edge shows in the gutter: dimmed, inert, a click widens the frame to it.
    bool peeking(const std::string &panel) const;
    bool handlePeekButton(const PointerButtonEvent &event);
    std::string m_peekPressed;
    struct Tile { double amount = 0, velocity = 0, target = 0; QString icon; bool idle = false; };
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
    std::unique_ptr<krellis::DragSession> m_session;
    EffectWindow *m_dragWindow = nullptr;
    RectF m_dragStartGeometry;
    int m_simForceFloat = -1;
    krellis::config::SpawnTarget m_spawnTarget = krellis::config::SpawnTarget::Pointer; // where a new tiled window opens
    std::string m_lastActivePanel; // the panel of the last tracked window to be activated (KWin activates a new window before windowAdded)
    double m_peekPx = 40; // gutter of neighbouring tiles left in view around a zoomed framing; 0 = off
    bool m_closeFocus = true;  // closing the focused window focuses its layout neighbour
    bool m_floatFirst = false; // floating-first mode: new windows float, and drops tile only at screen edges and corners
    krellis::Point m_tileMinimum{1, 1}; // layout floor of an app without a rule ([General] tileMinWidth/tileMinHeight)
    int m_iconBelow = 0;   // tiles drawn narrower than this show an icon instead of the window; 0 = never
    double m_landMs = 200; // glide off: duration of the move-and-scale a window plays when its real geometry changes; 0 = none
    struct Landing { TRect from; double start; };
    std::map<EffectWindow *, Landing> m_landings;
    bool m_glide = false; // our own animations (camera spring, layout tween, appear/close/popup); off = windows jump straight to their final rects
    bool m_shiftHeld = false; // from the latest pointer event; Shift floats a dragged window
    bool floatModifierHeld() const;
    bool m_dragFromKWin = false;
    krellis::RectMap m_lastRects; // what was drawn last frame; rebuilt by refreshRects()
    void refreshRects();
    std::unique_ptr<class OffscreenQuickScene> m_slot;
    bool m_overlaysCovered = false; // a fullscreen window is up: the overlay scenes are hidden, they would draw over it
    double m_slotOpacity = 0;
    // Action toast: text, the frame-clock time it started, and whether it replaces a visible one.
    std::unique_ptr<class OffscreenQuickScene> m_toast;
    QString m_toastText;
    double m_toastStartMs = 0;
    bool m_toastPending = true; // start the clock on the next frame (m_nowMs can be small, so no sentinel value)
    bool m_toastResume = false;
    // The frame clock only runs while a toast fades or slides. The hold waits on a real-time timer (the animation
    // clock advances at most one frame per tick, so it cannot time a pause); m_toastGen drops a stale timer.
    bool m_toastBusy = false, m_toastHoldArmed = false, m_toastHoldDone = false;
    int m_toastGen = 0;
    void updateToast();
    std::unique_ptr<class OffscreenQuickScene> m_sheet;
    QVariantList m_sheetGroups;
    bool m_sheetWanted = false;
    double m_sheetOpacity = 0;
    bool m_sheetBusy = false; // fading in or out; a fully shown sheet needs no frames
    void updateSheet(double dt);
    // Quick-jump palette: m_paletteOn means it has the keyboard and pointer; the scene outlives that by its fade-out.
    std::unique_ptr<class OffscreenQuickScene> m_palette;
    std::vector<PaletteItem> m_paletteItems;
    std::vector<size_t> m_paletteRanked; // indices into m_paletteItems, best first
    QString m_paletteQuery;
    int m_paletteSel = 0, m_paletteFirst = 0; // rank of the selected row and of the first visible one
    bool m_paletteOn = false, m_paletteDirty = false, m_paletteBusy = false;
    double m_paletteOpacity = 0, m_paletteWheel = 0;
    static constexpr int PALETTE_ROWS = 8;
    void updatePalette(double dt);
    void rankPalette();
    void paletteMove(int delta);
    void paletteGo(int rank);
    bool paletteKey(const KeyboardKeyEvent &e);
    void ensurePaletteScene();
    // A pinned window keeps its slot in the layout. While the slot is mostly out of view the window is drawn live as a
    // thumbnail in the bottom-left corner instead, above everything. Paint and input both go through visualRect, so the
    // thumbnail is where the window is for the pointer too (a click on it reveals the slot).
    struct Pin {
        std::string panel;
        QPointer<EffectWindow> window; // outlives the panel when the window moves to another output, so it can be un-elevated
        bool shown = false;            // currently drawn as a thumbnail
    };
    std::vector<Pin> m_pins;
    static constexpr double PIN_SHOW_BELOW = 0.4, PIN_HIDE_ABOVE = 0.6; // share of the slot in view; the gap stops flicker
    double pinVisibility(const std::string &panel) const; // share of the slot inside the work area at the camera's destination
    std::optional<TRect> pinRect(const std::string &panel) const;
    TRect shownRect(const std::string &panel) const { return pinRect(panel).value_or(belowTabs(panel, screenRect(panel))); }
    void updatePins();
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
    double m_bandOpacity = 0, m_candOpacity = 0, m_squeezeOpacity = 0;
    QRectF m_squeezeHint;  // the strip a squeezed edge tile would leave as a drawer (screen coordinates)
    int m_squeezeEdge = 0;
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
class KrellisEffect : public Effect
{
    Q_OBJECT
public:
    KrellisEffect();
    ~KrellisEffect() override;
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
    void stashActive(); // slide the active window into the nearest edge as a drawer; again docks it back
    void flipActive();
    void toggleHideActive();
    void closeActive();
    // Keyboard navigation: direction is left, right, up or down.
    void focusDirection(const QString &direction);
    void moveDirection(const QString &direction);
    bool resizeStep(const QString &direction, double fraction);
    void toggleResizeMode();
    void undo();
    void redo();
    // Scratchpad: floating overlays summoned over the current view and dismissed again. An empty name means any.
    void toggleScratch(const QString &name);
    void toggleScratchPad() { toggleScratch(QString()); }
    void toggleScratchMarkSlot() { toggleScratchMarkActive(); }
    bool toggleScratchMarkActive(); // false when the window can't become one (the only tile) or comes from a rule
    int scratchCount() const;
    // One slot per direction for the global shortcuts (QAction wants a no-argument slot).
    void focusLeft() { focusDirection(QStringLiteral("left")); }
    void focusRight() { focusDirection(QStringLiteral("right")); }
    void focusUp() { focusDirection(QStringLiteral("up")); }
    void focusDown() { focusDirection(QStringLiteral("down")); }
    void moveLeft() { moveDirection(QStringLiteral("left")); }
    void moveRight() { moveDirection(QStringLiteral("right")); }
    void moveUp() { moveDirection(QStringLiteral("up")); }
    void moveDown() { moveDirection(QStringLiteral("down")); }
    bool resizeModeActive() const { return m_resizeMode; }
    void turnOff();
    QString version() const;
    void toast(const QString &text); // on the output the user is working on
    QString dumpTheme() const { return m_theme.dump(); }
    void toggleBar();
    void toggleFloatFirst();
    void setFloatFirst(bool on);
    bool floatFirst() const;
    void setMapPinned(bool on);
    void toggleMapPinned();
    bool mapPinned() const;
    void toggleMap();
    void saveView();
    void pinActive();
    void nextTab();
    void previousTab();
    void splitTab();
    void groupLeft() { if (m_tabsOn) groupDirection(QStringLiteral("left")); }
    void groupRight() { if (m_tabsOn) groupDirection(QStringLiteral("right")); }
    void groupUp() { if (m_tabsOn) groupDirection(QStringLiteral("up")); }
    void groupDown() { if (m_tabsOn) groupDirection(QStringLiteral("down")); }
    EffectWindow *pinUnder(QPointF p) const;
    bool pinButton(const PointerButtonEvent &e);
    void togglePalette(); // the quick-jump palette: type part of a window title, app or saved view, and go there
    void back();
    void forward();
    void zoom(double factor, double px, double py);
    void finishGesture();
    // Synthetic input for the nested test harness (no input injector is available there).
    bool simButton(double x, double y, bool press, int modifiers);
    bool simMiddleButton(double x, double y, bool press);
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
    void reloadSettings();
    bool saveSession();
    QString sessionStatus() const;
    void restoreSession();
private:
    void applySettings(const krellis::config::Settings &settings);
    void syncOutputs();
    void initializeSession();
    void scheduleSave();
    void sessionWindowOpened();
    void markSessionInteraction();
    bool writeSession();
    QString outputKey(LogicalOutput *output) const;
    krellis::SessionSnapshot m_savedSession;
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
        KrellisCanvas *canvas;
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
    std::vector<QPointer<EffectWindow>> m_scratchMarks; // run-time marks; rule-flagged windows are found through the canvases
    std::vector<EffectWindow *> scratchWindows() const;
    QVariantMap scratchState() const;
    void refreshScratchUi();
    QPointer<EffectWindow> m_scratchLast; // the scratch window that had focus last, raised on top when the layer comes up
    bool m_scratchBusy = false;           // showing or hiding the layer: activations in between must not dismiss it
    bool scratchLayerShown() const;
    void showScratch(const std::vector<EffectWindow *> &windows);
    void hideScratch(const std::vector<EffectWindow *> &windows);
    void scratchActivated(EffectWindow *w);
    void scratchClick(QPointF point);
    KrellisCanvas *paletteCanvas() const; // the canvas whose palette is open, if any
    bool paletteButton(const PointerButtonEvent &e);
    bool m_swallowRelease = false; // a click went to the palette: its release must not reach a window
    bool m_resizeMode = false; // arrow keys resize the active tile; see handleResizeKey
    QTimer *m_resizeModeTimer = nullptr;
    void setResizeMode(bool on);
    bool handleResizeKey(const KeyboardKeyEvent &e);
    bool m_grabDecoration = false;
    QPointF m_decorPosition;
    void registerShortcuts();
    void trackChord(const KeyboardKeyEvent &e);
    void showHint();
    void hideHint(); // the hold turned into something else: drop the sheet and stay quiet until the chord is released
    void hideHintUi();
    QVariantList helpGroups() const;
    krellis::ChordHint m_chord;
    QTimer *m_chordTimer = nullptr;
    Qt::KeyboardModifiers m_simMods; // held modifiers of simKey, standing in for the compositor's keyboard state
    bool m_whichKey = true;
    void syncScratchShortcuts(); // Meta+G and Meta+Shift+G exist only while the scratchpad is enabled
    QList<QAction *> m_scratchActions;
    bool m_scratchpad = false;
    void syncPinShortcut(); // Meta+Shift+P exists only while pin is enabled
    QList<QAction *> m_pinActions;
    bool m_pinOn = false;
    void syncTabShortcuts(); // the tab chords exist only while tabs are enabled
    QList<QAction *> m_tabActions;
    bool m_tabsOn = false;
private:
    void groupDirection(const QString &direction);
    void transfer(EffectWindow *window, KrellisCanvas *target);
    KrellisCanvas *outputBeside(KrellisCanvas *from, krellis::Edge dir) const;
    KrellisCanvas *canvasAt(QPointF point) const;
    KrellisCanvas *owner(EffectWindow *window) const;
    KrellisCanvas *current() const;
    krellis::Theme m_theme; // before the canvases: their scenes read it while they are destroyed
    std::vector<std::unique_ptr<KrellisCanvas>> m_canvases;
    std::unique_ptr<KrellisInput> m_input;
    KrellisCanvas *m_pointerCanvas = nullptr, *m_pinchCanvas = nullptr;
    LogicalOutput *m_selectedOutput = nullptr;
    krellis::config::SpawnTarget m_spawnTarget = krellis::config::SpawnTarget::Pointer; // loaded from krellisrc; mirrored into every canvas
    double m_peekPx = 40;         // likewise ([General] peek, pixels)
    bool m_closeFocus = true;  // loaded from krellisrc; mirrored into every canvas
    bool m_floatFirst = false; // loaded from krellisrc; mirrored into every canvas
    bool m_mapPinned = false;  // likewise
    bool m_showMap = true;     // [General] map, default on; the minimap shortcut flips and saves it
    krellis::Point m_tileMinimum{1, 1}; // loaded from krellisrc; mirrored into every canvas
    int m_iconBelow = 0;   // loaded from krellisrc ([General] iconBelow); mirrored into every canvas
    double m_landMs = 200; // loaded from krellisrc ([General] landMs); mirrored into every canvas
    bool m_toastsOn = true, m_flyToOn = false; // loaded from krellisrc; mirrored into every canvas
    bool m_glide = false; // loaded from krellisrc ([General] glide, default off); mirrored into every canvas
};

} // namespace KWin
