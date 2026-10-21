// Spring and tween primitives for the camera and layout motion (port of Trellis runtime/motion).
#pragma once

#include "types.h"

#include <functional>
#include <map>
#include <string>

namespace trellis {

struct Motion {
    static constexpr double stiffness = 210, damping = 29, maxStep = 0.032, epsilon = 0.0001;
    static constexpr double layoutMs = 460, pickupMs = 280, appearMs = 220;
};

double easeOutQuint(double t);
Rect lerpRect(const Rect &a, const Rect &b, double t);
bool sameRectEps(const Rect &a, const Rect &b, double eps = 1e-9);

/// A spring over a rect. Deterministic: step(dtSeconds).
class RectSpring
{
public:
    explicit RectSpring(const Rect &initial = {0, 0, 1, 1}) : value(initial), target(initial) {}
    Rect value, target, velocity{0, 0, 0, 0};
    bool moving() const;
    /// Returns true while still moving.
    bool step(double dt);
    void finish();
    void jump(const Rect &rect);
};

/// Per-element transitions from a captured rect to a live target.
class LayoutTween
{
public:
    void begin(std::map<std::string, Rect> from, double nowMs);
    bool step(double nowMs, double durationMs = Motion::layoutMs);
    void stop();
    bool active() const { return progress < 1; }
    Rect apply(const std::string &id, const Rect &target) const;

private:
    std::map<std::string, Rect> from;
    double start = 0, progress = 1;
};

/// CSS cubic-bezier timing function.
std::function<double(double)> cubicBezier(double x1, double y1, double x2, double y2);
extern const std::function<double(double)> DOCK_EASE; // (.2,.75,.2,1)
inline constexpr double DOCK_HIDE_MS = 300, DOCK_RESTORE_MS = 380;

} // namespace trellis
