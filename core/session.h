#pragma once
#include "document.h"
#include <tuple>
namespace krellis {
// A stable app identity, optionally refined by an application-supplied role.
// Deliberately excludes captions, process IDs and connection-local window UUIDs.
struct WindowIdentity {
    std::string app, role;
    bool operator<(const WindowIdentity &other) const { return std::tie(app, role) < std::tie(other.app, other.role); }
    bool operator==(const WindowIdentity &) const = default;
};
// Saved index -> live index. Both sides must have exactly one candidate.
std::map<size_t, size_t> uniqueWindowMatches(const std::vector<WindowIdentity> &saved, const std::vector<WindowIdentity> &live);
// Prune unmatched panels and replace matched panels with their current live IDs.
LayoutDocument rebindSession(const LayoutDocument &saved, const std::map<std::string, NodePtr> &panels);
}
