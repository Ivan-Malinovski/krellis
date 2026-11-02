// Krellis' motion primitives, backed by KWin's own spring simulation.
//
// `core/motion.h` keeps the rectangle spring's API but no longer integrates anything itself: this file supplies the
// per-axis simulation from KWin::SpringMotion, so the camera and the tile fades move exactly like KWin's own
// slide/overview animations instead of a hand-written integrator. KWin's damping is a ratio, Krellis' tuning
// constants are the classic stiffness/damping pair, so the ratio is derived rather than duplicated.
#pragma once

#include "kwin_springmotion.h"
#include "motion.h"

#include <cmath>

namespace krellis
{

/// KWin's spring, exposed to the model as a plain 1-D spring. `advance` takes seconds because the model ticks in
/// frame time; KWin::SpringMotion advances in whole milliseconds, so the remainder is carried to the next tick.
class KwinSpring final : public Spring1D
{
public:
    explicit KwinSpring(double initial = 0)
        : m_spring(Motion::stiffness, dampingRatio())
    {
        m_spring.setEpsilon(Motion::epsilon);
        m_spring.setAnchor(initial);
        m_spring.setPosition(initial);
    }

    void setAnchor(double anchor) override { m_spring.setAnchor(anchor); }

    void advance(double seconds) override
    {
        m_pendingMs += seconds * 1000.0;
        constexpr double tick = 1.0;
        if (m_pendingMs < tick) return;
        const auto whole = std::chrono::milliseconds(static_cast<long long>(m_pendingMs / tick));
        m_pendingMs -= double(whole.count());
        m_spring.advance(whole);
    }

    double position() const override { return m_spring.position(); }
    double velocity() const override { return m_spring.velocity(); }
    bool moving() const override { return m_spring.isMoving(); }

    void reset(double position, double velocity) override
    {
        m_spring.setPosition(position);
        m_spring.setVelocity(velocity);
    }

private:
    /// KWin's damping ratio that matches Krellis' damping coefficient: c = 2 * sqrt(k) * zeta.
    static double dampingRatio() { return Motion::damping / (2.0 * std::sqrt(Motion::stiffness)); }

    KWin::SpringMotion m_spring;
    double m_pendingMs = 0;
};

/// The factory the effect hands to every RectSpring; core has no implementation of its own.
SpringFactory kwinSpringFactory();

} // namespace krellis
