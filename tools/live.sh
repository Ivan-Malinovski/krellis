#!/usr/bin/env bash
# Load or unload the installed effect in the running desktop. Nothing is written to kwinrc, so a
# logout or KWin restart also clears it. If KWin crashes twice soon after loading it, the effect refuses to load
# (crash-loop guard); "reset" clears that, "on" does too. Emergency exit from the keyboard: Meta+Alt+Escape.
# Each tools/install.sh installs under a fresh plugin id (krellis_<stamp>), because KWin never re-reads a
# plugin file it has already mapped. "on" swaps whatever Krellis is loaded for the newest installed one.
set -euo pipefail
plugins=/usr/lib/qt6/plugins/kwin/effects/plugins
marker=${XDG_STATE_HOME:-$HOME/.local/state}/krellis/boot.json  # crash-loop marker, see effect/boot_guard.h
effects() { qdbus6 org.kde.KWin /Effects "$@"; }
loaded_ids() { effects org.freedesktop.DBus.Properties.Get org.kde.kwin.Effects loadedEffects | grep -E '^[kt]rellis(_[0-9]+)?$' || true; }
newest_file() { ls -t "$plugins"/krellis*.so 2>/dev/null | head -1; }
unload_all() { for id in $(loaded_ids); do effects org.kde.kwin.Effects.unloadEffect "$id" >/dev/null; done; }
case "${1:-status}" in
  on)
    file=$(newest_file); [ -n "$file" ] || { echo 'Nothing installed. Run tools/install.sh first.' >&2; exit 1; }
    unload_all
    rm -f "$marker" # a deliberate manual load is a fresh start for the crash-loop guard
    if [ "$(effects org.kde.kwin.Effects.loadEffect "$(basename "$file" .so)")" != true ]; then
      echo 'KWin refused to load Krellis (see: journalctl --user -b -t kwin_wayland | grep -i krellis).' >&2; exit 1
    fi;;
  login)
    # For login startup: wait for KWin, then load the newest install. Unlike "on" this
    # keeps the crash-loop marker, so safe mode and the KWin-version check still stop a bad build at login.
    file=$(newest_file); [ -n "$file" ] || { echo 'Nothing installed.' >&2; exit 1; }
    for _ in $(seq 60); do effects org.freedesktop.DBus.Properties.Get org.kde.kwin.Effects loadedEffects >/dev/null 2>&1 && break; sleep 1; done
    [ -z "$(loaded_ids)" ] || exit 0
    [ "$(effects org.kde.kwin.Effects.loadEffect "$(basename "$file" .so)")" = true ] \
      || { echo 'KWin did not load Krellis (safe mode, KWin version mismatch or a bad build).' >&2; exit 1; };;
  reset) rm -f "$marker"; echo 'Crash-loop marker cleared.';;
  off) unload_all;;
  status)
    ids=$(loaded_ids)
    if [ -z "$ids" ]; then
      echo 'not loaded'
      strikes=$(grep -o '"strikes":[0-9]*' "$marker" 2>/dev/null | grep -o '[0-9]*' || true)
      [ "${strikes:-0}" -ge 2 ] && echo "safe mode: the crash-loop guard recorded $strikes early crashes and keeps Krellis off. Run tools/live.sh reset, then on."
      exit 0
    fi
    echo "loaded: $(echo $ids)"
    running=$(qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.version 2>/dev/null || echo 'unknown (older than version stamps)')
    file=$(newest_file)
    installed=$(grep -ao 'build [A-Z][a-z][a-z] [ 0-9][0-9] 20[0-9][0-9] [0-9:]*' "$file" | head -1)
    echo "running:   $running"; echo "installed: $installed ($(basename "${file:-none}"))"
    [ "$running" = "$installed" ] || echo 'MISMATCH: the loaded Krellis is not the newest installed one. Run tools/live.sh on.';;
  *) echo 'usage: live.sh on|off|login|reset|status' >&2; exit 1;;
esac
