#!/usr/bin/env bash
# A float can be dragged past the usable area to the screen's edge, under a panel; tiles stay out of the panel.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
project=/home/ivan/dev/krellis
c++ -fPIC "$project/scenarios/panel.cpp" -o "$NEST_WORK/panel" $(pkg-config --cflags --libs Qt6Gui) -lLayerShellQtInterface
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
frame() { call dump | sed -n "s/^$1 .* frame \([0-9.-]*\),\([0-9.-]*\) \([0-9.]*\)x\([0-9.]*\) .*/\1 \2 \3 \4/p" | head -1; }
for n in A B; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
"$NEST_WORK/panel" bottom >"$NEST_OUT/panel.log" 2>&1 & panel=$!
sleep 2
echo "$(call dump | grep '^area')"
call toggleFloat 1 >/dev/null; sleep 1.5
read -r x y w h < <(frame p2)
echo "float at $x $y $w $h"
call simDragStart 1 $((${x%.*} + ${w%.*} / 2)) $((${y%.*} + 12)) | grep -q true || echo "FAIL could not grab the float"
for yy in 300 500 700 780 799; do call simDragMove $((${x%.*} + ${w%.*} / 2)) $yy; sleep 0.05; done
sleep 0.4; call dump | grep -E 'DRAG|SLOT'
call simDragEnd true; sleep 1.5
read -r x y w h < <(frame p2)
echo "dropped at $x $y $w $h"
bottom=$(awk -v y="$y" -v h="$h" 'BEGIN{printf "%d", y+h}')
echo "bottom edge $bottom"
call dump | grep '^area'
[ "$bottom" -gt 740 ] && echo "PASS the float reaches under the panel" || echo "FAIL the float stops above the panel (bottom $bottom)"
[ "$bottom" -le 800 ] && echo "PASS but stays on screen" || echo "FAIL leaves the screen"
"$project/tools/shot" float-under-panel >/dev/null
kill $panel 2>/dev/null; pkill -P "$$" -x alacritty
