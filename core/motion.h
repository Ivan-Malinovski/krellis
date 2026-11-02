// Spring and tween primitives for the camera and layout motion (port of Trellis runtime/motion).
#pragma once

#include "types.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace krellis {

struct Motion {
    static constexpr double stiffness = 210, damping = 29, maxStep = 0.032, epsilon = 0.0001;
    static constexpr double layoutMs = 460, pickupMs = 280, appearMs = 220;
};

double easeOutQuint(double t);
Rect lerpRect(const Rect &a, const Rect &b, double t);
bool sameRectEps(const Rect &a, const Rect &b, double eps = 1e-9);

// Hyprland v0.54.0 example preset. Geometry and alpha have independent clocks.
// These are screen-space presentation animations, independent of client resizing.
namespace HyprlandMotion {
inline constexpr double moveMs = 479, inMs = 410, outMs = 149;
inline constexpr double fadeInMs = 173, fadeOutMs = 146;
double ease(double progress); // Bézier (.23,1,.32,1), not the polynomial easeOutQuint
double fade(double progress); // Bézier (.5,.5,.75,1)
Rect collapsed(const Rect &rect, const Rect &output, const std::string &style);
}

/// One dimension of a spring. `krellis_core` stays free of KWin, so the simulation is supplied by the host: the
/// effect backs it with KWin's own `SpringMotion` (effect/kwin_spring.cpp) rather than a second implementation.
class Spring1D
{
public:
    virtual ~Spring1D() = default;
    /// The value the spring is pulled towards. Setting it keeps the current value and velocity, so an interrupted
    /// flight continues instead of restarting.
    virtual void setAnchor(double anchor) = 0;
    /// Advance the simulation by `seconds`.
    virtual void advance(double seconds) = 0;
    virtual double position() const = 0;
    virtual double velocity() const = 0;
    virtual bool moving() const = 0;
    /// Force value and velocity (jump, stop, and the initial state).
    virtual void reset(double position, double velocity) = 0;
};

/// Creates four independent springs for one RectSpring.
using SpringFactory = Spring1D *(*)();

/// A spring over a rect. Deterministic: step(dtSeconds). The four axes run on the host's spring.
class RectSpring
{
public:
    explicit RectSpring(SpringFactory make, const Rect &initial = {0, 0, 1, 1});
    ~RectSpring();
    RectSpring(const RectSpring &) = delete;
    RectSpring &operator=(const RectSpring &) = delete;

    // Mirrors of the springs, so "target = x" is how a caller asks for a new destination.
    Rect value, target, velocity{0, 0, 0, 0};
    bool moving() const;
    /// Returns true while still moving.
    bool step(double dt);
    void finish();
    void jump(const Rect &rect);
    /// Keep the current value but drop the velocity (a gesture takes over from here).
    void stop();

private:
    void apply(const Rect &rect, const Rect &speed);
    std::unique_ptr<Spring1D> m_spring[4];
};

/// Per-element transitions from a captured rect to a live target.
class LayoutTween
{
public:
    void begin(std::map<std::string, Rect> from, double nowMs, double durationMs = Motion::layoutMs);
    bool step(double nowMs, double durationMs = -1);
    void stop();
    bool active() const { return progress < 1; }
    Rect apply(const std::string &id, const Rect &target) const;

private:
    std::map<std::string, Rect> from;
    double start = 0, progress = 1, duration = Motion::layoutMs, now = 0;
    struct Sample { Rect value, velocity; double time; };
    mutable std::map<std::string, Sample> samples;
    std::map<std::string, Rect> initialVelocity;
};

/// CSS cubic-bezier timing function.
std::function<double(double)> cubicBezier(double x1, double y1, double x2, double y2);
extern const std::function<double(double)> DOCK_EASE; // (.2,.75,.2,1)
inline constexpr double DOCK_HIDE_MS = 300, DOCK_RESTORE_MS = 380;

} // namespace krellis
