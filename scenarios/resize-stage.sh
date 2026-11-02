#!/usr/bin/env bash
# A stage wrapper must be transparent when locating an ancestor resize divider.
#
# The delta is positive on purpose. resize.sh drags resize-alpha's right (inner) edge, and alpha carries
# --large-min (a 1206x2292 client minimum), so it is already held at its minimum width: a *shrinking* drag
# (KRELLIS_RESIZE_DELTA=-65) is clamped away by KWin, which then emits no interactiveMoveResizeStepped at all
# and resize.sh's "nativeSteps [1-9]" check aborts before it reaches any of the stage-wrapper assertions.
# Growing the tile changes the real geometry, so the native steps arrive and the whole of resize.sh runs.
set -euo pipefail
export KRELLIS_RESIZE_DELTA=65
cat >"$XDG_CONFIG_HOME/krellisrc" <<'RULES'
[ViewRules]
resize-alpha\placement=stage
RULES
qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.reloadRules
exec bash "$(dirname "$0")/resize.sh"
