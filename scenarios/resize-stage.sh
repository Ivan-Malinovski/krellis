#!/usr/bin/env bash
# A stage wrapper must be transparent when locating an ancestor resize divider.
set -euo pipefail
export KRELLIS_RESIZE_DELTA=-65
cat >"$XDG_CONFIG_HOME/krellisrc" <<'RULES'
[ViewRules]
resize-alpha\placement=stage
RULES
qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.reloadRules
exec bash "$(dirname "$0")/resize.sh"
