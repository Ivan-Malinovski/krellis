// Document model: a clean-room C++ port of the Trellis layout model (schema 1).
// Nodes are immutable and shared; "unchanged" is pointer identity, like the reference's `===`.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace krellis {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

enum class Axis { X, Y };
enum class Edge { Left, Right, Top, Bottom };

inline Axis edgeAxis(Edge e) { return (e == Edge::Left || e == Edge::Right) ? Axis::X : Axis::Y; }
inline bool edgeBefore(Edge e) { return e == Edge::Left || e == Edge::Top; }
inline Edge oppositeEdge(Edge e)
{
    switch (e) {
    case Edge::Left: return Edge::Right;
    case Edge::Right: return Edge::Left;
    case Edge::Top: return Edge::Bottom;
    default: return Edge::Top;
    }
}

struct Node;
using NodePtr = std::shared_ptr<const Node>;

/// A split (weighted row/column), a panel (tab group) or the stage (never collapses, optional child).
struct Node {
    enum class Kind { Split, Panel, Stage };
    Kind kind = Kind::Panel;
    std::string id;
    // Split
    Axis axis = Axis::X;
    std::vector<double> weights;
    std::vector<NodePtr> children;
    // Panel
    std::vector<std::string> views;
    std::string selected;
    bool vacant = false; // an empty slot kept in the tree (float-first), not a panel to prune
    // Stage
    NodePtr child;
};

NodePtr makePanel(std::string id, std::vector<std::string> views, std::string selected = {});
NodePtr makeVacancy(std::string id);
NodePtr makeSplit(std::string id, Axis axis, std::vector<NodePtr> children, std::vector<double> weights = {});
NodePtr makeStage(std::string id, NodePtr child = nullptr);

enum class FloatingLayer { Stage, Overlay };

struct FloatingPanel {
    NodePtr panel;
    Rect rect;
    int z = 0;
    FloatingLayer layer = FloatingLayer::Overlay;
};

struct RestoreTarget {
    enum class Kind { Floating, Docked, Tab };
    Kind kind = Kind::Floating;
    Rect rect;                                   // floating
    FloatingLayer layer = FloatingLayer::Overlay; // floating
    std::string beside;                          // docked
    Edge edge = Edge::Left;                      // docked
    double share = 0.5;                          // docked
    std::string panel;                           // tab
};

struct HiddenPanel {
    NodePtr panel;
    RestoreTarget restore;
};

struct ViewRecord {
    std::string type;
    std::string title;
    // Free-form parameters live with the shell; the model only carries them through.
    std::map<std::string, std::string> params;
};

struct Framing {
    std::string id;
    std::string name;
    std::vector<std::string> frame;
};

struct Navigation {
    std::vector<std::string> frame;
    std::vector<Framing> framings;
};

/// How far an overlay float may reach past the usable area, as fractions of it: under a panel, to the screen edge.
struct FloatMargin {
    double left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const FloatMargin &) const = default;
};

struct LayoutDocument {
    NodePtr root;
    std::vector<FloatingPanel> floating;
    std::vector<HiddenPanel> hidden;
    std::map<std::string, ViewRecord> views;
    Navigation navigation;
    FloatMargin floatMargin;
};

/// Insertion-ordered map, matching JS `Map` iteration (a `set` on an existing key keeps its place).
template<typename V>
class OrderedMap
{
public:
    void set(const std::string &key, V value)
    {
        auto it = m_index.find(key);
        if (it == m_index.end()) {
            m_index.emplace(key, m_items.size());
            m_items.emplace_back(key, std::move(value));
        } else {
            m_items[it->second].second = std::move(value);
        }
    }
    bool has(const std::string &key) const { return m_index.count(key) != 0; }
    const V *find(const std::string &key) const
    {
        auto it = m_index.find(key);
        return it == m_index.end() ? nullptr : &m_items[it->second].second;
    }
    V *find(const std::string &key)
    {
        auto it = m_index.find(key);
        return it == m_index.end() ? nullptr : &m_items[it->second].second;
    }
    const V &at(const std::string &key) const { return m_items.at(m_index.at(key)).second; }
    size_t size() const { return m_items.size(); }
    bool empty() const { return m_items.empty(); }
    auto begin() const { return m_items.begin(); }
    auto end() const { return m_items.end(); }
    auto begin() { return m_items.begin(); }
    auto end() { return m_items.end(); }

private:
    std::vector<std::pair<std::string, V>> m_items;
    std::unordered_map<std::string, size_t> m_index;
};

} // namespace krellis
