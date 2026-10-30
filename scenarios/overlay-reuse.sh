#!/usr/bin/env bash
# Shared-process Kitty navigation must retain overview QML and hide inactive labels.
set -euo pipefail
exec bash "$KRELLIS_ROOT/scenarios/zoom-burst.sh"
