#!/usr/bin/env bash
# Scrolling over the gap between two tiles zooms one grid step per gesture (in and out) with no key held;
# scrolling over a window body does nothing.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
ok() { awk "BEGIN { print ($1) ? 1 : 0 }"; }
rect() { call dump | grep -F "'win-$1' screen" | sed -E "s/.*screen ([0-9.]+),([0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/"; }
camw() { call dump | grep -E "^camera" | sed -E 's/^camera [-0-9.e]+ [-0-9.e]+ ([-0-9.e]+) .*/\1/'; }
# Left-to-right x of window n.
left() { rect "$1" | cut -d' ' -f1; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call overview; sleep 1.5
# Whatever the spawn order, take the two left-most windows and the gap between them.
mapfile -t order < <(for n in A B C; do echo "$(left $n) $n"; done | sort -n | awk '{print $2}')
read -r ax ay aw ah < <(rect "${order[0]}"); read -r bx by bw bh < <(rect "${order[1]}")
sx=$(awk "BEGIN { print int(($ax + $aw + $bx) / 2) }"); sy=$(awk "BEGIN { print int($ay + $ah / 2) }")
bodyx=$(awk "BEGIN { print int($ax + 100) }"); bodyy=$(awk "BEGIN { print int($ay + 100) }")
echo "order ${order[*]}  gap at $sx,$sy"
w0=$(camw); echo "camera width $w0"
echo "--- scroll over a window body: nothing"
call simAxis -15 $bodyx $bodyy 0; sleep 1.2
check "body scroll leaves the zoom alone" "$(ok "$(camw) == $w0")" 1
echo "--- scroll in over the first gap: the two columns around it"
call simAxis -15 $sx $sy 0; sleep 1.5
w1=$(camw); echo "camera width $w1"
check "gap scroll in zooms in" "$(ok "$w1 < $w0 - 0.05")" 1
camx() { call dump | grep -E "^camera" | sed -E 's/^camera ([-0-9.e]+) .*/\1/'; }
gap() {
  read -r ax ay aw ah < <(rect "${order[0]}"); read -r bx by bw bh < <(rect "${order[1]}")
  sx=$(awk "BEGIN { print int(($ax + $aw + $bx) / 2) }"); sy=$(awk "BEGIN { print int($ay + $ah / 2) }")
}
SHIFT=$((0x02000000))
x1=$(camx)
echo "--- scroll in again over the same gap: the right-hand tile"
sleep 1; gap; echo "gap now at $sx,$sy"
call simAxis -15 $sx $sy 0; sleep 1.5
w2=$(camw); x2=$(camx); echo "camera x $x1 -> $x2, width $w1 -> $w2"
check "narrows to the right-hand tile" "$(ok "$x2 > $x1 + 0.05 && $w2 < $w1 - 0.05")" 1
echo "--- scroll out over the gap"
call overview; sleep 1.5; gap
call simAxis -15 $sx $sy 0; sleep 1.5; x1=$(camx); w1=$(camw)
sleep 1; gap
call simAxis -15 $sx $sy $SHIFT; sleep 1.5
w3=$(camw); x3=$(camx); echo "shift: camera x $x1 -> $x3, width $w1 -> $w3"
check "Shift narrows to the left-hand tile" "$(ok "$x3 < $x1 + 0.02 && $w3 < $w1 - 0.05")" 1
call overview; sleep 1.5; gap
call simAxis -15 $sx $sy 0; sleep 1.5; w1=$(camw); sleep 1; gap
call simAxis 15 $sx $sy 0; sleep 1.5
w4=$(camw); echo "out: width $w1 -> $w4"
check "gap scroll out zooms out" "$(ok "$w4 > $w1 + 0.05")" 1
pkill -P "$$" -x alacritty
