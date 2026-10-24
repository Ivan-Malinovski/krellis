// Camera navigation state machine (port of Trellis runtime/navigation, minus the DOM input binding).
//
// Frames any node or contiguous sibling range. Supports rubber-banded zoom (up to 1.35x the layout,
// 30% edge overshoot) that springs to the best fit after the gesture; maximize that restores the
// exact prior framing; overview that toggles back; history of visits by their views; saved framings
// that follow surviving views. Input (pinch, key-held drags, wheel) lives in the KWin effect and
// feeds zoom()/step()/finishGesture().
#pragma once

#include "motion.h"
#include "spatial.h"

#include <functional>

namespace krellis {

enum class NavMode { Off, Focus, Free };

struct NavigationHost {
    virtual ~NavigationHost() = default;
    virtual const LayoutDocument &doc() const = 0;
    virtual NavMode mode() const = 0;
    virtual bool reducedMotion() const { return false; }
    virtual Point viewport() const = 0; // {w, h} in pixels
    /// Pixels of the neighbouring tiles left in view on each side of a zoomed framing (0 = none).
    virtual double peekPadding() const { return 0; }
    virtual LayoutMetrics layoutMetrics() const = 0;
    virtual bool busy() const { return false; }
    /// Long jumps zoom out, pan and zoom in (needs the camera to glide).
    virtual bool flyTo() const { return false; }
    /// A real clock in ms, for rate-limiting flights (0 = none, never limits).
    virtual double clockMs() const { return 0; }
    virtual void schedule() {}
    virtual void changed() {}
    virtual void gestureChanged(bool /*active*/) {}
};

class Navigator
{
public:
    Navigator(NavigationHost &host, RectSpring &camera) : m_host(host), m_camera(camera) {}

    /// Recompute targets after a layout change; the framing follows its views.
    void sync();
    /// Advance the continuous camera arc toward its destination.
    void tick(double dtSeconds, double nowMs);
    bool flying() const { return m_pending.has_value(); }
    const std::optional<Rect> &pendingTarget() const { return m_pending; }
    /// Frame a node or sibling range (empty = the whole layout). Floats frame their stage.
    void focus(std::string id, bool record = true);
    bool toggle(const std::string &id); // maximize / restore
    void stepOut();
    void stepIn(std::optional<Point> worldPoint = std::nullopt);
    void back() { historyGo(-1); }
    void forward() { historyGo(1); }
    void overview();
    void toggleOverview();
    enum class FrameAll { All, Stage };
    void frame(FrameAll which);
    void frameIds(const std::vector<std::string> &ids);
    /// Frame exactly these tiles: their smallest node, boxed down to just them when that node holds more.
    void frameTiles(const std::vector<std::string> &leaves) { frameSet(leaves); }
    /// Bring a tile into view without disturbing the rest: nothing if it is framed, otherwise the frame keeps its
    /// size and slides to cover it. `preferWiden` (a window that just opened) takes the tile in beside the
    /// frame instead when that keeps the frame small enough.
    void reveal(const std::string &panelId, bool preferWiden = false);
    void include(const std::vector<std::string> &ids);

    // Gestures. `p` is the pointer as a fraction of the viewport (0..1).
    void zoom(double factor, Point p, Point panPixels = {0, 0});
    void finishGesture();
    void endGesture();
    /// Wheel stepping: one grid step per scroll gesture (a flick sends many events). Returns true if it stepped.
    bool wheelStep(double delta, Point worldPoint, double nowMs);
    void resetStepWheel();
    /// Zoom out grows the frame by one neighbouring column and one row, in: shrinks it by one of each. Both
    /// favour the side away from `at`, so what is under the pointer stays in view. False if nothing changed.
    bool zoomStep(bool out, Point at);
    /// The framing a release would choose right now (for the "Release to focus" preview).
    std::string gestureCandidate() const;

    Navigation serialize() const;
    void restore(const Navigation &nav);

    const Entries &entries() const { return m_entries; }
    const std::string &framed() const { return m_framed; }
    /// The tiles inside the framing; everything else is out of focus. Empty while showing everything.
    const std::vector<std::string> &framedLeaves() const { return m_framedLeaves; }
    /// A grid step can frame a block of tiles that is no node; `framed()` is then the smallest node holding it.
    bool boxed() const { return m_boxed; }
    bool overviewing() const { return m_framed.empty() && !m_boxed; }
    NodePtr framedNode() const;
    bool gestureActive() const { return m_gesture; }
    bool canGoBack() const { return m_visitIndex > 0; }
    bool canGoForward() const { return m_visitIndex + 1 < int(m_visits.size()); }
    std::string rootId() const;

    // Saved framings (NAV-14: blank names are rejected).
    std::optional<Framing> saveFraming(const std::string &name);
    void goFraming(const std::string &id);
    void removeFraming(const std::string &id);
    const std::vector<Framing> &framings() const { return m_framings; }

private:
    bool on() const { return m_host.mode() != NavMode::Off; }
    Rect rectOf(const std::string &id) const;
    Rect framedRect() const;
    /// The camera for a framing: the rect, widened by the peek gutter on each side that has tiles beyond it.
    Rect cameraFor(const Rect &frame) const;
    /// Frame these leaves: their smallest node, plus a box when that node holds more tiles than they cover.
    void frameSet(const std::vector<std::string> &leaves, bool record = true);
    void retarget();
    void slideTo(const std::string &panelId, bool preferWiden);
    void historyGo(int direction);

    NavigationHost &m_host;
    RectSpring &m_camera;
    Entries m_entries;
    std::string m_framed; // empty = the whole layout
    std::vector<std::string> m_framedLeaves; // the framed tiles (the box's tiles while boxed)
    bool m_boxed = false;
    std::vector<Visit> m_visits{{"", {}}};
    int m_visitIndex = 0;
    std::optional<MaximizeSession> m_session;
    std::optional<std::vector<std::string>> m_overviewReturn;
    std::vector<Framing> m_framings;
    bool m_gesture = false;
    std::optional<Rect> m_pending; // destination of the shallow camera arc
    Rect m_flightFrom{}, m_flightMid{};            // the current point along the camera arc
    double m_flightStartMs = 0, m_lastFlightMs = -1e18;
    double m_minTile = 0; // the smallest tile side, in layout units

    struct StepWheel {
        double last = -1e18;
        int direction = 0;
    } m_wheel;
};

} // namespace krellis
