#include "session.h"
#include <set>
namespace krellis {
std::map<size_t, size_t> uniqueWindowMatches(const std::vector<WindowIdentity> &saved, const std::vector<WindowIdentity> &live)
{
    std::map<WindowIdentity, std::vector<size_t>> previous, current;
    for (size_t i = 0; i < saved.size(); ++i) if (!saved[i].app.empty()) previous[saved[i]].push_back(i);
    for (size_t i = 0; i < live.size(); ++i) if (!live[i].app.empty()) current[live[i]].push_back(i);
    std::map<size_t, size_t> result;
    for (const auto &[key, indices] : previous)
        if (indices.size() == 1 && current[key].size() == 1) result[indices.front()] = current[key].front();
    return result;
}
LayoutDocument rebindSession(const LayoutDocument &saved, const std::map<std::string, NodePtr> &panels)
{
    LayoutDocument out;
    // Saved structure names and current panel names come from independent
    // sessions. Reserve both namespaces before choosing replacement names.
    std::set<std::string> liveIds, reserved;
    for (const auto &[previous, panel] : panels)
        if (panel) liveIds.insert(panel->id);
    reserved = liveIds;
    std::vector<NodePtr> structures;
    std::function<void(const NodePtr &)> collect = [&](const NodePtr &node) {
        if (!node) return;
        reserved.insert(node->id);
        if (node->kind == Node::Kind::Panel) return;
        structures.push_back(node);
        if (node->kind == Node::Kind::Stage) collect(node->child);
        else for (const auto &child : node->children) collect(child);
    };
    collect(saved.root);
    std::map<std::string, std::string> structuralIds;
    for (const auto &node : structures) {
        auto next = node->id;
        if (liveIds.contains(next)) {
            do { next = uid("session-node"); } while (reserved.contains(next));
            reserved.insert(next);
        }
        structuralIds[node->id] = std::move(next);
    }
    auto replacement = [&](const NodePtr &panel) -> NodePtr {
        if (!panel) return nullptr;
        const auto it = panels.find(panel->id);
        return it == panels.end() ? nullptr : it->second;
    };
    std::function<NodePtr(const NodePtr &)> tree = [&](const NodePtr &node) -> NodePtr {
        if (!node) return nullptr;
        if (node->kind == Node::Kind::Panel) return replacement(node);
        if (node->kind == Node::Kind::Stage) return makeStage(structuralIds.at(node->id), tree(node->child));
        std::vector<NodePtr> children;
        std::vector<double> weights;
        for (size_t i = 0; i < node->children.size(); ++i)
            if (auto child = tree(node->children[i])) { children.push_back(child); weights.push_back(i < node->weights.size() ? node->weights[i] : 1.0); }
        return children.empty() ? nullptr : normalize(makeSplit(structuralIds.at(node->id), node->axis, children, weights));
    };
    out.root = tree(saved.root);
    for (const auto &floating : saved.floating)
        if (const auto panel = replacement(floating.panel)) out.floating.push_back({panel, clampFloat(floating.rect, floating.layer), floating.z, floating.layer});
    auto id = [&](const std::string &previous) -> std::string {
        if (const auto it = panels.find(previous); it != panels.end()) return it->second->id;
        if (const auto it = structuralIds.find(previous); it != structuralIds.end())
            if (const auto node = findNode(out.root, it->second); node && node->kind != Node::Kind::Panel) return it->second;
        return {};
    };
    for (const auto &hidden : saved.hidden)
        if (const auto panel = replacement(hidden.panel)) {
            auto restore = hidden.restore;
            restore.beside = id(restore.beside);
            restore.panel = id(restore.panel);
            out.hidden.push_back({panel, restore});
        }
    auto frame = [&](const std::vector<std::string> &ids) {
        std::vector<std::string> next;
        for (const auto &previous : ids) if (auto mapped = id(previous); !mapped.empty()) next.push_back(mapped);
        return next;
    };
    out.navigation.frame = frame(saved.navigation.frame);
    for (const auto &framing : saved.navigation.framings) {
        const auto views = frame(framing.frame);
        if (!views.empty()) out.navigation.framings.push_back({framing.id, framing.name, views});
    }
    for (const auto &panel : allPanels(out))
        for (const auto &view : panel->views) out.views[view] = {"native", {}, {}};
    return sanitize(std::move(out));
}
}
