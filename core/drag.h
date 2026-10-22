// Window and tab dragging session (port of Trellis runtime/drag, minus DOM and pointer plumbing).
//
// The source stays in place at pickup; the lifted window eases to a compact card as it leaves.
// Drop targets settle for 150 ms and preview as a real reflow of the layout; the source collapses on
// the first target and never reopens. Seams and the frame band are targets; release commits the
// current candidate. Time is injected so the whole thing is deterministic under test.
#pragma once

#include "motion.h"
#include "spatial.h"

#include <map>
#include <optional>

namespace krellis {

inline const std::string DROP_SLOT = "__krellis-drop-slot";
inline const std::string SOURCE_SLOT = "__krellis-source-slot";

enum class Region { Stage, Side, Floating };

struct DropTarget {
    enum class Kind { Dock, Tab, Stage, Float };
    Kind kind = Kind::Float;
    DockTargetSpec spec;     // Dock
    std::string panel;       // Tab
    std::optional<int> index; // Tab
    std::string stage;       // Stage
    bool operator==(const DropTarget &o) const;
};

using RectMap = std::map<std::string, Rect>;

struct DragHost {
    virtual ~DragHost() = default;
    virtual bool reduced() const { return false; }
    virtual const LayoutDocument &doc() const = 0;
    /// Replace the document without committing (no change event, no history).
    virtual void setDoc(const LayoutDocument &) {}
    virtual Point viewport() const = 0;
    virtual double inset() const = 0;
    virtual double tabbarHeight(const Node &panel) const = 0;
    /// Height of the band along the top of a tile that takes a dropped window as a tab: the tab bar, or where it would be.
    virtual double tabDropHeight(const Node &panel) const { return tabbarHeight(panel); }
    /// Whether the body of a tile, away from its edges, also takes a dropped window as a tab (the original behaviour).
    virtual bool bodyTabs() const { return true; }
    /// Index a tab dropped at screen x would take in a panel's tab bar.
    virtual int tabIndexAt(const Node &panel, double x) const = 0;
    virtual Rect toScreen(const Rect &world) const = 0;
    virtual Point fromScreen(Point screen) const = 0;
    /// Screen rect of a docked node, inset by the gap, and its inverse.
    virtual Rect panelScreen(const Rect &world) const = 0;
    virtual std::optional<Rect> fromScreenRect(const std::optional<Rect> &screen) const = 0;
    virtual std::optional<Rect> floatWorld(const std::string &panelId) const = 0;
    /// The layer floats currently live in, if any.
    virtual std::optional<FloatingLayer> floatingLayer() const = 0;
    /// How far an overlay float may reach past the viewport (under a panel), as fractions of it.
    virtual FloatMargin floatMargin() const { return {}; }
    virtual bool frameOnly(const std::string &) const { return false; }
    virtual LayoutMetrics layoutMetrics() const = 0;
    virtual std::string collapsedGroupOf(const std::string &) const { return {}; }
    virtual bool collapsedSplit(const std::string &) const { return false; }
    virtual bool allowed(const std::vector<std::string> &, Region) const { return true; }
    /// Hosts without tab bars (separate compositor windows) refuse tab targets.
    virtual bool tabsAllowed() const { return true; }
    virtual NodePtr framedNode() const { return nullptr; }
    virtual Point minSize(const std::vector<std::string> &) const { return {0, 0}; }
    /// Current on-screen rects of panels (what the compositor is showing).
    virtual const RectMap &lastRects() const = 0;
    /// Previews tween from `previous` to the new layout targets.
    virtual void beginTween(const RectMap &previous, double nowMs) { (void)previous; (void)nowMs; }
};

struct DragOutcome {
    bool committed = false;      // the document changed
    bool wasDrag = false;        // a drag (not a click) ended
    LayoutDocument next;         // when committed
    RectMap from;                // screen rects to animate from (includes the lifted window)
    std::string settle;          // panel to settle into place ("" = none)
    std::string liftedId;
    bool fromFloating = false;
    std::optional<DropTarget::Kind> target;
};

class DragSession
{
public:
    static constexpr double Threshold = 6;
    static constexpr double SettleMs = 150;
    static constexpr double PickupMs = 280;
    static constexpr Point Compact{380, 260};
    static constexpr double DesktopSplitMaxPx = 64;
    static constexpr double MinSeamHitWidth = 8;
    static constexpr double OverlayEdgeBand = 0.14;
    /// A float dropped within SnapEdgePx of a viewport edge snaps to that half; within SnapCornerPx of a
    /// corner (and near an edge), to that quarter. The corner zone is wider so it can be hit on purpose.
    static constexpr double SnapEdgePx = 24;
    static constexpr double SnapCornerPx = 120;
    /// Float-first: a drop within DockEdgePx of a viewport edge docks across that edge; within DockCornerPx of
    /// a corner as well, it splits the tile in that corner instead. Anywhere else the window floats.
    static constexpr double DockEdgePx = 24;
    static constexpr double DockCornerPx = 120;

    /// Press on a panel's bar/frame (`viewId` empty) or on one of its tabs. `pointer` is in screen space.
    DragSession(DragHost &host, const std::string &panelId, const std::string &viewId, Point pointer);

    /// Pointer moved (screen space). Returns true once the drag is active.
    bool move(Point pointer, double nowMs);
    /// Advance timers: applies the settled candidate after 150 ms.
    void tick(double nowMs);
    /// Release or cancel. Release expresses intent immediately, even before the hover delay expires.
    DragOutcome end(bool commit, double nowMs);

    /// While set (a held modifier), the window drops as a float wherever the pointer is on screen,
    /// never docking, whatever it started as.
    void setForceFloat(bool on) { m_forceFloat = on; }
    /// Float-first mode: windows float wherever they are dropped, except at screen edges and corners, which
    /// dock. The force modifier then inverts it, so the drag resolves as in tiling-first mode.
    void setFloatFirst(bool on) { m_floatFirst = on; }
    bool active() const { return m_active; }
    const RectMap &layoutTargets() const { return m_layoutTargets; }
    const std::string &dropLabel() const { return m_dropLabel; }
    const std::optional<DropTarget> &target() const { return m_target; }
    const std::optional<DropTarget> &candidate() const { return m_candidate; }
    const NodePtr &lifted() const { return m_lifted; }
    /// The lifted window's screen rect: full size over its origin, a compact card elsewhere.
    Rect liftedRect(double nowMs);
    bool collapsed() const { return m_collapsed; }
    /// Where an overlay float would snap if dropped now (viewport coordinates), if the pointer is in a snap zone.
    std::optional<Rect> snapZone() const;
    const LayoutDocument &previewDoc() const { return m_pdoc; }

private:
    void activate(double nowMs);
    void retarget();
    std::optional<DropTarget> tabTarget(const NodePtr &panel, Point p, const Rect &screen) const;
    std::optional<Edge> overlayEdge(Point p, const Rect &r) const;
    std::optional<DropTarget> edgeDock() const;
    std::optional<DropTarget> filter(const std::optional<DropTarget> &next) const;
    bool leavesVacancy(const DropTarget &target) const;
    void settle(const std::optional<DropTarget> &next, double nowMs);
    void apply(const std::optional<DropTarget> &next, double nowMs);
    Rect seamOrigin(const Entries &preview, const Rect &slot) const;
    RectMap restingLayout() const;
    Rect collapsedSource() const;
    std::optional<Rect> worldOf(const std::string &id) const;
    std::optional<Rect> currentWorld(const std::string &id) const;
    Rect floatRect(const LayoutDocument &next, FloatingLayer layer) const;

    DragHost &m_host;
    bool m_forceFloat = false;
    bool m_floatFirst = false;
    Point m_start, m_pointer;
    bool m_active = false;
    LayoutDocument m_origin;
    NodePtr m_lifted;
    bool m_inDoc = true;
    std::string m_pendingTab;
    std::string m_excludedHost;
    std::optional<Rect> m_tabOrigin;
    bool m_tabRollback = false;
    std::optional<FloatingLayer> m_fromFloat;
    bool m_fromFrame = false;
    RectMap m_base;
    Rect m_sourceScreen;
    LayoutDocument m_pdoc;
    Entries m_dropBase;
    bool m_collapsed = false;
    RectMap m_layoutTargets;
    Rect m_floating;
    double m_pickupAt = 0, m_compactFrom = 0, m_compactTo = 0, m_compactAmount = 0, m_desktopAmount = 0;
    double m_sizeTick = 0;
    double m_grabX = 0.5, m_grabY = 0;
    std::optional<DropTarget> m_target, m_candidate;
    std::optional<double> m_targetDeadline;
    std::string m_dropLabel;
    double m_now = 0;
};

} // namespace krellis
