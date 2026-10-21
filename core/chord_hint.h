// The which-key hint: hold Meta+Alt alone for a moment and a cheat sheet appears; anything else dismisses it.
#pragma once

#include <cstdint>

namespace trellis {

class ChordHint
{
public:
    explicit ChordHint(std::int64_t delayMs = 400)
        : m_delay(delayMs)
    {
    }

    void setDelay(std::int64_t ms) { m_delay = ms; }

    /// Modifier state changed. `other` is any modifier besides Meta and Alt (Shift, Ctrl).
    void setModifiers(bool meta, bool alt, bool other, std::int64_t nowMs)
    {
        m_meta = meta;
        m_alt = alt;
        if (!meta && !alt)
            m_latched = false; // both let go: the next hold counts again
        if (meta && alt && !other) {
            if (!m_pending && !m_shown && !m_latched) {
                m_pending = true;
                m_since = nowMs;
            }
            return;
        }
        m_pending = m_shown = false;
    }

    /// A key or button went down (or a registered shortcut fired): the hold turned into something else.
    void otherInput() { dismiss(); }
    void dismiss()
    {
        m_pending = m_shown = false;
        m_latched = m_meta || m_alt; // still holding the chord: stay quiet until it is released
    }

    /// True when the hint just became visible.
    bool tick(std::int64_t nowMs)
    {
        if (m_pending && nowMs - m_since >= m_delay) {
            m_pending = false;
            m_shown = true;
            return true;
        }
        return false;
    }

    bool shown() const { return m_shown; }
    bool pending() const { return m_pending; }
    /// When tick() will show the hint, or -1.
    std::int64_t deadline() const { return m_pending ? m_since + m_delay : -1; }

private:
    std::int64_t m_delay, m_since = 0;
    bool m_meta = false, m_alt = false;
    bool m_pending = false, m_shown = false, m_latched = false;
};

} // namespace trellis
