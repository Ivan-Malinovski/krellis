#include "test_spring.h"

#include <algorithm>
#include <cmath>

namespace krellis
{
namespace
{

class EulerSpring final : public Spring1D
{
public:
    void setAnchor(double anchor) override { m_anchor = anchor; }

    void advance(double seconds) override
    {
        const double dt = std::clamp(seconds, 0.0, Motion::maxStep);
        m_velocity += (Motion::stiffness * (m_anchor - m_value) - Motion::damping * m_velocity) * dt;
        m_value += m_velocity * dt;
    }

    double position() const override { return m_value; }
    double velocity() const override { return m_velocity; }
    bool moving() const override { return std::abs(m_anchor - m_value) > Motion::epsilon || std::abs(m_velocity) > Motion::epsilon; }

    void reset(double position, double velocity) override
    {
        m_value = position;
        m_velocity = velocity;
    }

private:
    double m_value = 0, m_velocity = 0, m_anchor = 0;
};

} // namespace

SpringFactory testSpringFactory()
{
    return [] { return static_cast<Spring1D *>(new EulerSpring); };
}

} // namespace krellis
