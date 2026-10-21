#include "motion.h"

#include <algorithm>
#include <cmath>

namespace trellis {

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

void LayoutTween::begin(std::map<std::string, Rect> f, double nowMs)
{
    from = std::move(f);
    start = nowMs;
    progress = from.empty() ? 1 : 0;
}

bool LayoutTween::step(double nowMs, double durationMs)
{
    const double elapsed = durationMs > 0 ? (nowMs - start) / durationMs : 1;
    progress = easeOutQuint(elapsed);
    if (elapsed >= 1)
        stop();
    return progress < 1;
}

void LayoutTween::stop()
{
    progress = 1;
    from.clear();
}

Rect LayoutTween::apply(const std::string &id, const Rect &target) const
{
    auto it = from.find(id);
    return (it != from.end() && progress < 1) ? lerpRect(it->second, target, progress) : target;
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

} // namespace trellis
