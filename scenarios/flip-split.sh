#!/usr/bin/env bash
# flipActive turns the active window and its neighbour between side-by-side and stacked, and a middle-click on
# the gap between two tiles does the same for that pair. With three tiles only the pair flips.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
# Window n's screen rect as "x y w h".
# Evaluate a numeric test with awk (no bc here): ok "<expr>" prints 1 or 0.
ok() { awk "BEGIN { print ($1) ? 1 : 0 }"; }
rect() { call dump | grep -F "'win-$1' screen" | sed -E "s/.*screen ([0-9.]+),([0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/"; }
for n in A B; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call dump | head -8
read -r ax ay aw ah < <(rect A); read -r bx by bw bh < <(rect B)
echo "A $ax $ay $aw $ah   B $bx $by $bw $bh"
check "side by side to start" "$(ok "$bx > $ax + $aw - 1")" 1
echo "--- flip with the key: stacked"
call flipActive; sleep 1.5
read -r ax ay aw ah < <(rect A); read -r bx by bw bh < <(rect B)
echo "A $ax $ay $aw $ah   B $bx $by $bw $bh"
check "stacked: B below A" "$(ok "$by > $ay + $ah - 1")" 1
check "stacked: same column" "$(ok "$bx == $ax")" 1
/home/ivan/dev/krellis/tools/shot flip-stacked >/dev/null
echo "--- again: back"
call flipActive; sleep 1.5
read -r ax ay aw ah < <(rect A); read -r bx by bw bh < <(rect B)
check "side by side again" "$(ok "$bx > $ax + $aw - 1")" 1
echo "--- middle-click on the gap"
sx=$(awk "BEGIN { print int(($ax + $aw + $bx) / 2) }"); sy=$(awk "BEGIN { print int($ay + $ah / 2) }")
call simMiddleButton $sx $sy true >/dev/null; call simMiddleButton $sx $sy false >/dev/null; sleep 1.5
read -r ax ay aw ah < <(rect A); read -r bx by bw bh < <(rect B)
echo "A $ax $ay $aw $ah   B $bx $by $bw $bh"
check "seam click stacked them" "$(ok "$by > $ay + $ah - 1")" 1
echo "--- a middle-click on a window is left alone"
call simMiddleButton $(awk "BEGIN { print int($ax + 100) }") $(awk "BEGIN { print int($ay + 100) }") true >/dev/null; call simMiddleButton $(awk "BEGIN { print int($ax + 100) }") $(awk "BEGIN { print int($ay + 100) }") false >/dev/null; sleep 1
read -r bx2 by2 bw2 bh2 < <(rect B)
check "not on a seam: nothing changed" "$bx2 $by2" "$bx $by"
echo "--- three tiles: only the active pair flips"
call flipActive; sleep 1.2 # A and B are stacked: put them in a row first
alacritty --title "win-C" -e sh -c "echo C; sleep 600" >/dev/null 2>&1 &
sleep 2.5
read -r ax ay aw ah < <(rect A); read -r bx by bw bh < <(rect B)
call flipActive; sleep 1.5
call dump | grep -F "' screen" | cut -c1-80
read -r ax2 ay2 aw2 ah2 < <(rect A); read -r bx3 by3 bw3 bh3 < <(rect B); read -r cx cy cw ch < <(rect C)
check "A keeps its place" "$ax2 $aw2" "$ax $aw"
check "A is untouched in height" "$ah2" "$ah"
check "C and B now share a column" "$cx" "$bx3"
check "C is above B" "$(ok "$by3 > $cy + $ch - 1")" 1
/home/ivan/dev/krellis/tools/shot flip-three >/dev/null
echo "--- and the key again puts the pair back side by side"
call flipActive; sleep 1.5
read -r bx4 by4 bw4 bh4 < <(rect B); read -r cx4 cy4 cw4 ch4 < <(rect C)
check "back in a row" "$(ok "$bx4 > $cx4 + $cw4 - 1 || $cx4 > $bx4 + $bw4 - 1")" 1
pkill -P "$$" -x alacritty
