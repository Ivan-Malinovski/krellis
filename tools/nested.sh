#!/usr/bin/env bash
# Run a throwaway nested kwin_wayland (own D-Bus, own config) with a plugin dir on its Qt plugin path.
# usage: nested.sh <plugin-root> <effect-ids,comma> <scenario-script> [width height]
# NEST_BACKEND=virtual renders to an in-memory framebuffer instead of opening a window on the outer
# session, so a test run cannot steal focus. NEST_BACKEND=drm drives the real GPU/display from a text VT (run from a tty login, never from the live desktop). Default (wayland) shows a window, needed for human preview.
# <plugin-root> must contain kwin/effects/plugins/*.so . The scenario script runs with
#   NEST_BUS (dbus addr is inherited), NEST_SOCKET and NEST_OUT (output dir) set.
set -euo pipefail
export NEST_SOCKET=${NEST_SOCKET:-krellis-nest}
if [ -S "${XDG_RUNTIME_DIR:?}/$NEST_SOCKET" ]; then
    echo "Nested socket $NEST_SOCKET is already in use" >&2
    exit 1
fi
# Refuse to run while a foreign compositor owns org.kde.KWin on the session bus. A kwin_wayland
# started with --virtual (or with its own --socket) outside dbus-run-session takes org.kde.KWin and
# org.kde.kglobalaccel away from the real desktop; KWin's GlobalShortcutsManager::init() then deletes
# its KGlobalAccelD for the life of that process with no retry (all global shortcuts dead, and
# plasma-shutdown cannot log out: "KWin failed to complete logout"). Our own bus is created below by
# dbus-run-session, so this only ever looks at the caller's bus. Exit status 1 means "dangerous owner".
# Tests only: KRELLIS_TEST_BUS_OWNER_CMDLINE=<cmdline> replaces the detected owner command line.
# Escape hatch: KRELLIS_ALLOW_SHARED_BUS=1 continues anyway (an intentional run).
guard_session_bus() {
    local pid line cmdline sock w i to="" dangerous=0
    local -a words
    if [ -n "${KRELLIS_TEST_BUS_OWNER_CMDLINE:-}" ]; then
        pid=test; cmdline=$KRELLIS_TEST_BUS_OWNER_CMDLINE
    else
        command -v busctl >/dev/null 2>&1 || command -v qdbus6 >/dev/null 2>&1 || return 0
        command -v timeout >/dev/null 2>&1 && to="timeout 5"
        if command -v busctl >/dev/null 2>&1; then
            line=$($to busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
                org.freedesktop.DBus GetConnectionUnixProcessID s org.kde.KWin 2>/dev/null) || line=""
        else
            line=$($to qdbus6 org.freedesktop.DBus / \
                org.freedesktop.DBus.GetConnectionUnixProcessID org.kde.KWin 2>/dev/null) || line=""
        fi
        pid=$(printf '%s\n' "$line" | awk '{print $NF}')
        case $pid in ''|*[!0-9]*) return 0 ;; esac  # nobody owns org.kde.KWin, or nothing to parse
        cmdline=$(tr '\0' ' ' 2>/dev/null <"/proc/$pid/cmdline" || true)
        if [ -z "${cmdline// /}" ]; then
            # Another user's (or a zombie) process: we cannot tell what it is, so warn but do not block.
            echo "warning: org.kde.KWin on the session bus is owned by pid $pid, whose /proc/$pid/cmdline is not readable." >&2
            echo "warning: if that is a stray kwin_wayland, global shortcuts and logout are broken until it exits." >&2
            return 0
        fi
    fi
    read -ra words <<<"$cmdline"
    sock=""
    for ((i = 0; i < ${#words[@]}; i++)); do
        case ${words[i]} in
            --socket) [ $((i + 1)) -lt ${#words[@]} ] && sock=${words[i + 1]} ;;
            --socket=*) sock=${words[i]#--socket=} ;;
        esac
    done
    case " $cmdline " in
        *" --virtual "*) dangerous=1 ;;
        *)
            # Only compare when we know this session's socket: over SSH WAYLAND_DISPLAY is unset and a
            # healthy desktop KWin would otherwise look foreign.
            if [ -n "$sock" ] && [ -n "${WAYLAND_DISPLAY:-}" ] && [ "$sock" != "$WAYLAND_DISPLAY" ]; then dangerous=1; fi
            ;;
    esac
    [ "$dangerous" = 1 ] || return 0  # plain desktop KWin (no --virtual, no foreign socket): healthy
    { echo "ERROR: a foreign compositor owns org.kde.KWin on this session bus."
      echo "  pid $pid: $cmdline"
      echo "It was not started with --socket \"\$WAYLAND_DISPLAY\" (${WAYLAND_DISPLAY:-unset}) and/or runs --virtual, so it is not your desktop KWin."
      echo "While it lives it holds org.kde.KWin and org.kde.kglobalaccel, KWin drops its KGlobalAccelD for good"
      echo "(no retry: every global shortcut in the real session dies) and plasma-shutdown cannot log out."
      echo "Nested tests must run under dbus-run-session; nested.sh does that for its own child only."
    } >&2
    return 1
}
if ! guard_session_bus; then
    if [ "${KRELLIS_ALLOW_SHARED_BUS:-0}" = 1 ]; then
        echo "KRELLIS_ALLOW_SHARED_BUS=1: continuing on a session bus owned by a test compositor." >&2
    else
        echo "Refusing to start a nested KWin. Kill the stray compositor, or set KRELLIS_ALLOW_SHARED_BUS=1 to run anyway." >&2
        exit 1
    fi
fi
export KRELLIS_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(realpath "$1"); effects=$2; scenario=$(realpath "$3"); W=${4:-1280}; H=${5:-800}
work=${NEST_WORK:-/tmp/krellis-nest}; rm -rf "$work"; mkdir -p "$work/config" "$work/out" "$work/data" "$work/state/krellis"
{
  echo "[Plugins]"
  IFS=, read -ra ids <<<"$effects"; for e in "${ids[@]}"; do echo "${e}Enabled=true"; done
  echo "blurEnabled=false"; echo "[Compositing]"; echo "Backend=OpenGL"
} > "$work/config/kwinrc"
# Optional read-only SVG decoration assets for rendering/cache regressions.
# Selecting a plugin here changes only this throwaway compositor's kwinrc.
if [ -n "${NEST_AURORAE_THEME_DIR:-}" ]; then
  theme_dir=$(realpath "$NEST_AURORAE_THEME_DIR"); theme_name=$(basename "$theme_dir")
  [[ -d "$theme_dir" && "$theme_name" =~ ^[[:alnum:]_.-]+$ && "$theme_name" != . && "$theme_name" != .. ]]
  mkdir -p "$work/data/aurorae/themes"
  ln -s "$theme_dir" "$work/data/aurorae/themes/$theme_name"
  printf '\n[org.kde.kdecoration2]\nlibrary=%s\ntheme=__aurorae__svg__%s\n' "${NEST_DECORATION_LIBRARY:-org.kde.kwin.aurorae.v2}" "$theme_name" >> "$work/config/kwinrc"
fi
if [ -n "${NEST_KRELLISRC:-}" ]; then cp "$NEST_KRELLISRC" "$work/config/krellisrc"; else printf "[General]\nglide=true\nmap=false\nfollowTheme=false\ntoasts=false\nwhichKey=false\n" >"$work/config/krellisrc"; fi
# NEST_KDEGLOBALS: a colour scheme for the nested session (theme scenarios); without it the built-in palette is used.
if [ -n "${NEST_KDEGLOBALS:-}" ]; then cp "$NEST_KDEGLOBALS" "$work/config/kdeglobals"; fi
# NEST_BOOT_STRIKES=N: seed the crash-loop marker as if the last start died N times in a row, just now (safe-mode scenario).
if [ -n "${NEST_BOOT_STRIKES:-}" ]; then
  printf '{"startedAtMs":%s,"cleanExit":false,"survived":false,"strikes":%s,"notified":true}\n' "$(( $(date +%s%3N) - 1000 ))" "$NEST_BOOT_STRIKES" >"$work/state/krellis/boot.json"
fi
if [ -n "${NEST_SESSION_FILE:-}" ]; then cp "$NEST_SESSION_FILE" "$work/state/krellis/session.json"; fi
export NEST_OUTPUTS=${NEST_OUTPUTS:-1}
export NEST_BACKEND_FLAG=
case "${NEST_BACKEND:-wayland}" in virtual) NEST_BACKEND_FLAG=--virtual;; drm) NEST_BACKEND_FLAG=--drm;; esac
export NEST_OUT="$work/out" NEST_WORK="$work"
exec dbus-run-session -- bash -c '
  export XDG_STATE_HOME="$NEST_WORK/state"
  export XDG_CONFIG_HOME="$NEST_WORK/config" XDG_DATA_HOME="$NEST_WORK/data" XDG_CACHE_HOME="$NEST_WORK/cache"
  export QT_PLUGIN_PATH="'"$root"'${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
  export KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 QT_LOGGING_RULES="kwin_*.debug=false;krellis*.debug=true"
  export QT_FORCE_STDERR_LOGGING=1
  kwin_wayland $NEST_BACKEND_FLAG --socket "$NEST_SOCKET" --output-count "$NEST_OUTPUTS" --width '"$W"' --height '"$H"' --no-lockscreen --no-global-shortcuts >"$NEST_OUT/kwin.log" 2>&1 &
  kw=$!
  for i in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/$NEST_SOCKET" ] && break; sleep 0.2; done
  sleep 1
  bash '"$scenario"'; rc=$?
  kill $kw 2>/dev/null; wait $kw 2>/dev/null; exit $rc
'
