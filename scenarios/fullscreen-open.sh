#!/usr/bin/env bash
# A window that opens already fullscreen (Spectacle's region overlay, a game) is left alone instead of taking a tile.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
for n in A B; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
check "two tiles" "$(order)" "p1 p2"
alacritty --title "win-full" -o 'window.startup_mode="Fullscreen"' -e sh -c "sleep 600" >/dev/null 2>&1 &
sleep 3
check "a fullscreen opener takes no tile" "$(order)" "p1 p2"
check "no panel for it" "$(call dump | grep -c "^p[0-9]")" 2
check "the tiles keep their size" "$(call dump | grep "^p1 " | sed -n 's/.* screen [0-9.,]* \([0-9.]*x[0-9.]*\) .*/\1/p')" "628.0x788.0"
pkill -P "$$" -x alacritty
