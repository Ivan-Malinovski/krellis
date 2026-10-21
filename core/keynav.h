// Keyboard navigation over the tiled layout: focus, move and resize by direction.
// Directions reuse Edge: Left/Right/Top/Bottom mean "towards that side".
#pragma once

#include "document.h"
#include "tree.h"

#include <optional>

namespace trellis {

/// The panel nearest to `from` towards `dir`, or empty. Candidates lie wholly past the origin's edge; the
/// one sharing the most of the origin's extent wins, then the nearest along the axis, then the nearest centre.
std::string neighbourInDirection(const Entries &entries, const std::string &from, Edge dir, bool skipVacant = true);

/// Move a docked panel one step: swap with a neighbouring sibling, else dock beside the neighbour on the
/// facing edge, else (nothing that way) pop it out to that edge of the whole layout. Unchanged when it
/// cannot move; `moved` tells whether it did.
LayoutDocument moveInDirection(LayoutDocument doc, const std::string &panelId, Edge dir, const LayoutMetrics &metrics,
                               bool *moved = nullptr);

/// Move the divider on the `dir` side of the panel one step towards `dir` (growing the panel); when the
/// panel has no neighbour that way, its opposite divider moves instead (shrinking it). `fraction` is the
/// step as a share of the panel's own size. Returns the new root, or null when no divider can move.
NodePtr resizeInDirection(const NodePtr &root, const LayoutMetrics &metrics, const std::string &panelId, Edge dir,
                          double fraction);

} // namespace trellis
