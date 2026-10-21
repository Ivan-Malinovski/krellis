#!/usr/bin/env bash
# Run a throwaway nested kwin_wayland (own D-Bus, own config) with a plugin dir on its Qt plugin path.
# usage: nested.sh <plugin-root> <effect-ids,comma> <scenario-script> [width height]
# NEST_BACKEND=virtual renders to an in-memory framebuffer instead of opening a window on the outer
# session, so a test run cannot steal focus. NEST_BACKEND=drm drives the real GPU/display from a text VT (run from a tty login, never from the live desktop). Default (wayland) shows a window, needed for human preview.
# <plugin-root> must contain kwin/effects/plugins/*.so . The scenario script runs with
#   NEST_BUS (dbus addr is inherited), NEST_SOCKET and NEST_OUT (output dir) set.
set -euo pipefail
export NEST_SOCKET=${NEST_SOCKET:-trellis-nest}
if [ -S "${XDG_RUNTIME_DIR:?}/$NEST_SOCKET" ]; then
    echo "Nested socket $NEST_SOCKET is already in use" >&2
    exit 1
fi
root=$(realpath "$1"); effects=$2; scenario=$(realpath "$3"); W=${4:-1280}; H=${5:-800}
work=${NEST_WORK:-/tmp/trellis-nest}; rm -rf "$work"; mkdir -p "$work/config" "$work/out" "$work/data" "$work/state/kwin-trellis"
{
  echo "[Plugins]"
  IFS=, read -ra ids <<<"$effects"; for e in "${ids[@]}"; do echo "${e}Enabled=true"; done
  echo "blurEnabled=false"; echo "[Compositing]"; echo "Backend=OpenGL"
} > "$work/config/kwinrc"
if [ -n "${NEST_TRELLISRC:-}" ]; then cp "$NEST_TRELLISRC" "$work/config/trellisrc"; else printf "[General]\nglide=true\nmap=false\n" >"$work/config/trellisrc"; fi
if [ -n "${NEST_SESSION_FILE:-}" ]; then cp "$NEST_SESSION_FILE" "$work/state/kwin-trellis/session.json"; fi
export NEST_OUTPUTS=${NEST_OUTPUTS:-1}
export NEST_BACKEND_FLAG=
case "${NEST_BACKEND:-wayland}" in virtual) NEST_BACKEND_FLAG=--virtual;; drm) NEST_BACKEND_FLAG=--drm;; esac
export NEST_OUT="$work/out" NEST_WORK="$work"
exec dbus-run-session -- bash -c '
  export XDG_STATE_HOME="$NEST_WORK/state"
  export XDG_CONFIG_HOME="$NEST_WORK/config" XDG_DATA_HOME="$NEST_WORK/data" XDG_CACHE_HOME="$NEST_WORK/cache"
  export QT_PLUGIN_PATH="'"$root"'${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
  export KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 QT_LOGGING_RULES="kwin_*.debug=false;trellis*.debug=true"
  export QT_FORCE_STDERR_LOGGING=1
  kwin_wayland $NEST_BACKEND_FLAG --socket "$NEST_SOCKET" --output-count "$NEST_OUTPUTS" --width '"$W"' --height '"$H"' --no-lockscreen --no-global-shortcuts >"$NEST_OUT/kwin.log" 2>&1 &
  kw=$!
  for i in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/$NEST_SOCKET" ] && break; sleep 0.2; done
  sleep 1
  bash '"$scenario"'; rc=$?
  kill $kw 2>/dev/null; wait $kw 2>/dev/null; exit $rc
'
