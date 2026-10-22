// Bringing an older layout snapshot up to date with the windows that exist now (undo/redo).
#pragma once

#include "document.h"

#include <set>
#include <string>

namespace krellis {

/// `snapshot` is the layout to go back to, `now` the live document. The result has the snapshot's arrangement, but:
/// - windows that have since closed are removed (a closed window is never resurrected);
/// - windows the snapshot never saw, windows whose hidden/minimized state differs (that state belongs to the compositor,
///   not to the user's edit) and panels in `fromNow` (scratch windows) keep the place they have in `now`;
///   a docked one lands at the end seam;
/// - `floatMargin`, `navigation` and `views` come from `now`, and floats that exist in both keep their stacking order.
/// `sanitize` is not used: a live document has no `views` records, so it would drop everything.
LayoutDocument reconcile(const LayoutDocument &snapshot, const LayoutDocument &now, const std::set<std::string> &fromNow = {});

} // namespace krellis
