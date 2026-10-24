#include "motion.h"

#include <algorithm>
#include <cmath>

namespace krellis {

double easeOutQuint(double t)
{
    return 1 - std::pow(1 - std::min(1.0, std::max(0.0, t)), 5);
}

Rect lerpRect(const Rect &a, const Rect &b, double t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t};
}

bool sameRectEps(const Rect &a, const Rect &b, double eps)
{
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.w - b.w) < eps
        && std::abs(a.h - b.h) < eps;
}

bool RectSpring::moving() const
{
    return !sameRectEps(value, target) || !sameRectEps(velocity, Rect{0, 0, 0, 0});
}

bool RectSpring::step(double dt)
{
    dt = std::min(dt > 0 ? dt : 0.016, Motion::maxStep);
    double error = 0;
    double *v[] = {&value.x, &value.y, &value.w, &value.h};
    double *t[] = {&target.x, &target.y, &target.w, &target.h};
    double *vel[] = {&velocity.x, &velocity.y, &velocity.w, &velocity.h};
    for (int i = 0; i < 4; ++i) {
        *vel[i] += ((*t[i] - *v[i]) * Motion::stiffness - *vel[i] * Motion::damping) * dt;
        *v[i] += *vel[i] * dt;
        error += std::abs(*t[i] - *v[i]) + std::abs(*vel[i]) * 0.1;
    }
    if (error <= Motion::epsilon) {
        finish();
        return false;
    }
    return true;
}

void RectSpring::finish()
{
    value = target;
    velocity = {0, 0, 0, 0};
}

void RectSpring::jump(const Rect &rect)
{
    target = rect;
    finish();
}

void LayoutTween::begin(std::map<std::string, Rect> f, double nowMs, double durationMs)
{
    initialVelocity.clear();
    // Captures come from apply(): carry the actual velocity through an interrupted glide.
    for (const auto &[id, rect] : f) {
        const auto it = samples.find(id);
        if (active() && it != samples.end() && nowMs - it->second.time <= Motion::maxStep * 1000
            && sameRectEps(rect, it->second.value, 1e-5))
            initialVelocity[id] = it->second.velocity;
    }
    samples.clear();
    from = std::move(f);
    start = now = nowMs;
    duration = durationMs;
    progress = from.empty() || duration <= 0 ? 1 : 0;
}

bool LayoutTween::step(double nowMs, double durationMs)
{
    now = nowMs;
    if (durationMs >= 0) duration = durationMs;
    progress = duration > 0 ? std::clamp((nowMs - start) / duration, 0.0, 1.0) : 1;
    if (progress >= 1) stop();
    return active();
}

void LayoutTween::stop()
{
    progress = 1;
    from.clear();
    samples.clear();
    initialVelocity.clear();
}

Rect LayoutTween::apply(const std::string &id, const Rect &target) const
{
    const auto it = from.find(id);
    if (it == from.end() || !active()) return target;
    const Rect &a = it->second;
    const auto velocity = initialVelocity.find(id);
    const Rect v = velocity == initialVelocity.end()
        ? Rect{3 * (target.x - a.x) / duration, 3 * (target.y - a.y) / duration,
               3 * (target.w - a.w) / duration, 3 * (target.h - a.h) / duration}
        : velocity->second;
    const double t = progress, u = 1 - t;
    const double h = t * t * (3 - 2 * t), tangent = t * u * u;
    auto component = [&](double x, double y, double speed) {
        return x + (y - x) * h + speed * duration * tangent;
    };
    auto derivative = [&](double x, double y, double speed) {
        return (y - x) * 6 * t * u / duration + speed * u * (1 - 3 * t);
    };
    Rect value{component(a.x, target.x, v.x), component(a.y, target.y, v.y),
               component(a.w, target.w, v.w), component(a.h, target.h, v.h)};
    Rect speed{derivative(a.x, target.x, v.x), derivative(a.y, target.y, v.y),
               derivative(a.w, target.w, v.w), derivative(a.h, target.h, v.h)};
    // A reversal can carry an edge past its target, but must never invert a rectangle.
    if (value.w < 0) { value.w = 0; speed.w = 0; }
    if (value.h < 0) { value.h = 0; speed.h = 0; }
    samples[id] = {value, speed, now};
    return value;
}

std::function<double(double)> cubicBezier(double x1, double y1, double x2, double y2)
{
    auto sample = [](double a, double b, double t) {
        return ((1 - 3 * b + 3 * a) * t + (3 * b - 6 * a)) * t * t + 3 * a * t;
    };
    return [=](double x) {
        if (x <= 0)
            return 0.0;
        if (x >= 1)
            return 1.0;
        double lo = 0, hi = 1, t = x;
        for (int i = 0; i < 24; ++i) {
            const double v = sample(x1, x2, t);
            if (std::abs(v - x) < 1e-5)
                break;
            if (v < x)
                lo = t;
            else
                hi = t;
            t = (lo + hi) / 2;
        }
        return sample(y1, y2, t);
    };
}

const std::function<double(double)> DOCK_EASE = cubicBezier(0.2, 0.75, 0.2, 1);

} // namespace krellis
