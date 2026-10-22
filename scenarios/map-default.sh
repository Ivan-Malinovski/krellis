#!/usr/bin/env bash
# The minimap is on by default (no [General] map key); Meta+Alt+M turns it off and the choice is saved.
# Seed: NEST_KRELLISRC with only "[General]\nglide=true".
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
shown() { call dump | sed -n "s/^MAP .* shown \([01]\) .*/\1/p" | head -1; }
for n in A B; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
check "minimap is on by default" "$(shown)" 1
/home/ivan/dev/krellis/tools/shot map-default >/dev/null
call toggleMap >/dev/null; sleep 1.2
v=$(shown); check "toggle turns it off" "${v:-0}" 0
check "the choice is saved" "$(grep -c '^map=false' "$XDG_CONFIG_HOME/krellisrc")" 1
call toggleMap >/dev/null; sleep 1.2
check "toggle turns it back on" "$(shown)" 1
pkill -P "$$" -x alacritty
