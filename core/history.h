// Undo/redo for layout edits. Documents share structure, so keeping one per step is cheap.
#pragma once

#include "types.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace krellis {

/// Whether two documents lay out the same: same tree, same floats (panel, rect, layer, z), same hidden panels.
inline bool sameLayout(const LayoutDocument &a, const LayoutDocument &b)
{
    if (a.root != b.root || a.floating.size() != b.floating.size() || a.hidden.size() != b.hidden.size())
        return false;
    for (size_t i = 0; i < a.floating.size(); ++i) {
        const auto &x = a.floating[i], &y = b.floating[i];
        if (x.panel != y.panel || !(x.rect == y.rect) || x.z != y.z || x.layer != y.layer)
            return false;
    }
    for (size_t i = 0; i < a.hidden.size(); ++i)
        if (a.hidden[i].panel != b.hidden[i].panel)
            return false;
    return true;
}

class History
{
public:
    struct Entry {
        LayoutDocument doc;
        std::string label;
    };

    explicit History(size_t cap = 50, std::int64_t coalesceMs = 600)
        : m_cap(cap)
        , m_coalesceMs(coalesceMs)
    {
    }

    /// Remember `before` as the state an edit started from. Edits with the same non-empty `key` that follow each other
    /// within the coalescing window (a held resize key) share the first entry. A new edit drops the redo steps.
    void record(LayoutDocument before, std::string label, const std::string &key = {}, std::int64_t nowMs = 0)
    {
        m_redo.clear();
        if (!key.empty() && !m_undo.empty() && m_lastKey == key && nowMs - m_lastMs <= m_coalesceMs) {
            m_lastMs = nowMs;
            return;
        }
        m_undo.push_back({std::move(before), std::move(label)});
        if (m_undo.size() > m_cap)
            m_undo.pop_front();
        m_lastKey = key;
        m_lastMs = nowMs;
    }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    size_t size() const { return m_undo.size(); }
    size_t redoSize() const { return m_redo.size(); }
    const std::string &undoLabel() const { return m_undo.back().label; }
    const std::string &redoLabel() const { return m_redo.back().label; }

    /// The state to go back to; `now` becomes the redo step.
    std::optional<Entry> undo(const LayoutDocument &now)
    {
        if (m_undo.empty())
            return std::nullopt;
        Entry e = std::move(m_undo.back());
        m_undo.pop_back();
        m_redo.push_back({now, e.label});
        m_lastKey.clear();
        return e;
    }
    std::optional<Entry> redo(const LayoutDocument &now)
    {
        if (m_redo.empty())
            return std::nullopt;
        Entry e = std::move(m_redo.back());
        m_redo.pop_back();
        m_undo.push_back({now, e.label});
        m_lastKey.clear();
        return e;
    }
    void clear()
    {
        m_undo.clear();
        m_redo.clear();
        m_lastKey.clear();
    }

private:
    std::deque<Entry> m_undo, m_redo;
    size_t m_cap;
    std::int64_t m_coalesceMs;
    std::string m_lastKey;
    std::int64_t m_lastMs = 0;
};

} // namespace krellis
