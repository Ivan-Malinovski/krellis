// Shake detection: a pointer swung back and forth along one axis, fast enough and far enough, within a short time.
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>

namespace krellis {

class ShakeDetector
{
public:
    /// `swing` logical pixels between turns, `turns` direction changes within `windowMs`.
    explicit ShakeDetector(double swing = 80, int turns = 4, std::int64_t windowMs = 600)
        : m_swing(swing)
        , m_turns(turns)
        , m_window(windowMs)
    {
    }

    void reset()
    {
        m_x = {};
        m_y = {};
    }

    /// Feed one pointer sample; true on the sample that completes a shake. The history is cleared when it fires.
    bool feed(double x, double y, std::int64_t nowMs)
    {
        const bool a = m_x.feed(x, nowMs, m_swing, m_window, m_turns);
        const bool b = m_y.feed(y, nowMs, m_swing, m_window, m_turns);
        if (a || b) {
            reset();
            return true;
        }
        return false;
    }

private:
    struct Axis
    {
        bool started = false;
        double extreme = 0; // furthest value reached in the current direction
        int dir = 0;        // +1 / -1 once a direction is known
        std::deque<std::int64_t> turns;

        bool feed(double v, std::int64_t now, double swing, std::int64_t window, int need)
        {
            if (!started) {
                started = true;
                extreme = v;
                return false;
            }
            if (dir == 0) {
                if (std::abs(v - extreme) >= swing) {
                    dir = v > extreme ? 1 : -1;
                    extreme = v;
                }
                return false;
            }
            if ((v - extreme) * dir > 0) {
                extreme = v; // still going the same way
            } else if (std::abs(v - extreme) >= swing) {
                dir = -dir; // turned back after a full swing
                extreme = v;
                turns.push_back(now);
            }
            while (!turns.empty() && now - turns.front() > window)
                turns.pop_front();
            return int(turns.size()) >= need;
        }
    };

    double m_swing;
    int m_turns;
    std::int64_t m_window;
    Axis m_x, m_y;
};

} // namespace krellis
