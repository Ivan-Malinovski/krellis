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

namespace trellis {

enum class NavMode { Off, Focus, Free };

struct NavigationHost {
    virtual ~NavigationHost() = default;
    virtual const LayoutDocument &doc() const = 0;
    virtual NavMode mode() const = 0;
    virtual bool reducedMotion() const { return false; }
    virtual Point viewport() const = 0; // {w, h} in pixels
    virtual LayoutMetrics layoutMetrics() const = 0;
    virtual bool busy() const { return false; }
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
    void ensureVisible(const std::string &panelId);
    void include(const std::vector<std::string> &ids);

    // Gestures. `p` is the pointer as a fraction of the viewport (0..1).
    void zoom(double factor, Point p, Point panPixels = {0, 0});
    void finishGesture();
    void endGesture();
    /// Wheel stepping: deltas add up until worth a step. Returns true if it stepped.
    bool wheelStep(double delta, Point worldPoint, double nowMs);
    void resetStepWheel();
    /// The framing a release would choose right now (for the "Release to focus" preview).
    std::string gestureCandidate() const;

    Navigation serialize() const;
    void restore(const Navigation &nav);

    const Entries &entries() const { return m_entries; }
    const std::string &framed() const { return m_framed; }
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
    void retarget();
    void historyGo(int direction);

    NavigationHost &m_host;
    RectSpring &m_camera;
    Entries m_entries;
    std::string m_framed; // empty = the whole layout
    std::vector<std::string> m_framedLeaves;
    std::vector<Visit> m_visits{{"", {}}};
    int m_visitIndex = 0;
    std::optional<MaximizeSession> m_session;
    std::optional<std::vector<std::string>> m_overviewReturn;
    std::vector<Framing> m_framings;
    bool m_gesture = false;

    struct StepWheel {
        double last = -1e18, total = 0, steppedAt = -1e18;
        int direction = 0;
    } m_wheel;
};

} // namespace trellis
