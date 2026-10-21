#!/usr/bin/env bash
# A stage wrapper must be transparent when locating an ancestor resize divider.
set -euo pipefail
export TRELLIS_RESIZE_DELTA=-65
cat >"$XDG_CONFIG_HOME/trellisrc" <<'RULES'
[ViewRules]
resize-alpha\placement=stage
RULES
qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect.reloadRules
exec bash "$(dirname "$0")/resize.sh"
