# Minimap cogwheel: click it, the settings panel opens over the card; click rows to flip pointer placement
# (dwindle takes over, so three windows halve the focused tile) and a plain key (urgent); click the cog again to close.
# Seeded with map-settings.rc (map pinned open).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
rc() { sed -n "s/^$1=//p" "$XDG_CONFIG_HOME/krellisrc"; }
open() { call dump | sed -nE 's/^MAPSET open ([01]).*/\1/p' | head -1; }
centre() { # centre of the cog or of a row, as "x y"
  if [ "$1" = cog ]; then call dump | sed -nE 's/^MAPSET open [01] cog ([0-9.]+),([0-9.]+) ([0-9.]+)x([0-9.]+)/\1 \2 \3 \4/p' | head -1
  else call dump | sed -nE "s/^MAPSETROW $1 at ([0-9.]+),([0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/p" | head -1; fi |
  awk '{printf "%d %d", $1 + $3/2, $2 + $4/2}'
}
click() { call simButton "$1" "$2" true 0 >/dev/null; call simButton "$1" "$2" false 0 >/dev/null; sleep 0.8; }
alacritty --title win-A -e sh -c "sleep 600" >/dev/null 2>&1 & sleep 3
check "closed at first" "$(open)" 0
read -r cx cy <<<"$(centre cog)"; echo "cog at $cx,$cy"
click "$cx" "$cy"
check "open after the cog" "$(open)" 1
$KRELLIS_ROOT/tools/shot map-settings-open >/dev/null 2>&1 || true
call dump | grep -E "^MAPSETROW"
read -r rx ry <<<"$(centre spawn)"; click "$rx" "$ry"
check "pointer placement off in the file" "$(rc spawnTarget)" dwindle
check "panel stays open" "$(open)" 1
read -r rx ry <<<"$(centre urgent)"; click "$rx" "$ry"
check "urgent off in the file" "$(rc urgent)" false
check "row reads off" "$(call dump | sed -nE 's/^MAPSETROW urgent .* on ([01])/\1/p' | head -1)" 0
$KRELLIS_ROOT/tools/shot map-settings-flipped >/dev/null 2>&1 || true
for n in B C; do alacritty --title "win-$n" -e sh -c "sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
check "dwindle order p1 p2 p3" "$(call dump | sed -n 's/^ORDER //p')" "p1 p2 p3"
read -r rx ry <<<"$(centre spawn)"; click "$rx" "$ry"
check "pointer again" "$(rc spawnTarget)" pointer
read -r cx cy <<<"$(centre cog)"; click "$cx" "$cy"
check "closed by the cog" "$(open)" 0
pkill -P "$$" -x alacritty
