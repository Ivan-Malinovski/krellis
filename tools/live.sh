#!/usr/bin/env bash
# Load or unload the installed effect in the running desktop. Nothing is written to kwinrc, so a
# logout or KWin restart also clears it. Emergency exit from the keyboard: Meta+Alt+Escape.
# Each tools/install.sh installs under a fresh plugin id (trellis_<stamp>), because KWin never re-reads a
# plugin file it has already mapped. "on" swaps whatever Trellis is loaded for the newest installed one.
set -euo pipefail
plugins=/usr/lib/qt6/plugins/kwin/effects/plugins
effects() { qdbus6 org.kde.KWin /Effects "$@"; }
loaded_ids() { effects org.freedesktop.DBus.Properties.Get org.kde.kwin.Effects loadedEffects | grep -E '^trellis(_[0-9]+)?$' || true; }
newest_file() { ls -t "$plugins"/trellis*.so 2>/dev/null | head -1; }
unload_all() { for id in $(loaded_ids); do effects org.kde.kwin.Effects.unloadEffect "$id" >/dev/null; done; }
case "${1:-status}" in
  on)
    file=$(newest_file); [ -n "$file" ] || { echo 'Nothing installed. Run tools/install.sh first.' >&2; exit 1; }
    unload_all
    effects org.kde.kwin.Effects.loadEffect "$(basename "$file" .so)";;
  off) unload_all;;
  status)
    ids=$(loaded_ids)
    if [ -z "$ids" ]; then echo 'not loaded'; exit 0; fi
    echo "loaded: $(echo $ids)"
    running=$(qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect.version 2>/dev/null || echo 'unknown (older than version stamps)')
    file=$(newest_file)
    installed=$(grep -ao 'build [A-Z][a-z][a-z] [ 0-9][0-9] 20[0-9][0-9] [0-9:]*' "$file" | head -1)
    echo "running:   $running"; echo "installed: $installed ($(basename "${file:-none}"))"
    [ "$running" = "$installed" ] || echo 'MISMATCH: the loaded Trellis is not the newest installed one. Run tools/live.sh on.';;
  *) echo 'usage: live.sh on|off|status' >&2; exit 1;;
esac
