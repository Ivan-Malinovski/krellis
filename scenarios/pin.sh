#!/usr/bin/env bash
# Pin: a pinned tile keeps its slot, and while the slot is mostly out of view the window is drawn live as a thumbnail in
# the bottom-left corner. A click on the thumbnail reveals the slot; unpinning sends the window home.
# Seeded with scenarios/pin.rc (spawnTarget=end, peek=0) so the framing is the tile itself.
# Drives the coordinator through pinActive/simButton, so the global chord (Meta+Shift+P) is not covered: check it live.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
active() { call dump | sed -n 's/^ACTIVE //p'; }
framed() { call dump | sed -nE "s/^framed '(.*)'/\1/p"; }
line() { call dump | grep "^$1 "; }
pinstate() { line "$1" | grep -o 'PIN-SHOWN\|PIN' | head -1; }
drawn() { line "$1" | sed -nE 's/.* drawn ([0-9.]+)x([0-9.]+) drawnpos ([-0-9.]+),([-0-9.]+).*/\1 \2 \3 \4/p'; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
for n in A B C D; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
call toggleMaximize 3; sleep 1.5
check "one tile framed" "$(framed)" p4
check "start active" "$(active)" p4
echo "--- pin and unpin"
check "not pinned at first" "$(pinstate p4)" ""
call pinActive; sleep 0.5
check "pinned" "$(pinstate p4)" PIN
call pinActive; sleep 0.5
check "unpinned" "$(pinstate p4)" ""
call pinActive; sleep 0.5
check "pinned again" "$(pinstate p4)" PIN
echo "--- the slot leaves the screen"
call focusDirection left; sleep 1.6
check "slid to the neighbour" "$(framed)" p3
check "thumbnail shown" "$(pinstate p4)" PIN-SHOWN
read -r tw th tx ty <<<"$(drawn p4)"
echo "thumbnail $tw x $th at $tx,$ty"
check "thumbnail is smaller than the screen" "$(awk -v w="$tw" 'BEGIN{print (w > 100 && w < 640) ? "yes" : "no"}')" yes
check "thumbnail is at the left edge" "$(awk -v x="$tx" 'BEGIN{print (x >= 0 && x < 40) ? "yes" : "no"}')" yes
check "thumbnail is in the lower half" "$(awk -v y="$ty" 'BEGIN{print (y > 400) ? "yes" : "no"}')" yes
check "thumbnail is opaque" "$(line p4 | sed -nE 's/.* opacity ([0-9.]+).*/\1/p' | head -1)" 1.00
check "its slot stays put in the layout" "$(line p4 | sed -nE 's/^p4 .* screen ([-0-9.]+),.*/\1/p' | awk '{print ($1 > 1000 || $1 < -1000) ? "far" : "near"}')" far
$KRELLIS_ROOT/tools/shot pin >/dev/null
echo "--- a click reveals the slot"
cx=$(awk -v x="$tx" -v w="$tw" 'BEGIN{printf "%d", x + w/2}'); cy=$(awk -v y="$ty" -v h="$th" 'BEGIN{printf "%d", y + h/2}')
call simButton "$cx" "$cy" true 0 >/dev/null; call simButton "$cx" "$cy" false 0 >/dev/null; sleep 1.8
check "click jumped to the pinned tile" "$(active)" p4
check "and framed it" "$(framed)" p4
check "thumbnail gone while the slot is in view" "$(pinstate p4)" PIN
echo "--- several pins stack"
call focusDirection left; sleep 1.6
call pinActive; sleep 0.4        # p3
call focusDirection left; sleep 1.6
call focusDirection left; sleep 1.6
check "at the far end" "$(framed)" p1
check "both thumbnails shown" "$(pinstate p4)$(pinstate p3)" PIN-SHOWNPIN-SHOWN
read -r w4 h4 x4 y4 <<<"$(drawn p4)"
read -r w3 h3 x3 y3 <<<"$(drawn p3)"
check "same column" "$x4" "$x3"
check "the first pin is lowest" "$(awk -v a="$y4" -v b="$y3" 'BEGIN{print (a > b) ? "yes" : "no"}')" yes
check "they do not overlap" "$(awk -v a="$y4" -v b="$y3" -v h="$h3" 'BEGIN{print (b + h <= a) ? "yes" : "no"}')" yes
echo "--- a closing pin lets the stack close up"
call dump | grep -E "^p[0-9]" | sed 's/ frame.*//'
echo "--- unpin sends it home"
call focusDirection right; sleep 1.6
call pinActive; sleep 0.4        # unpin p2? only pinned ones toggle; this is p2 so it pins
call pinActive; sleep 0.4
call focusDirection right; sleep 1.6
check "p3 active" "$(active)" p3
call pinActive; sleep 1.2
check "p3 unpinned" "$(pinstate p3)" ""
call focusDirection left; sleep 1.6
call focusDirection left; sleep 1.6
check "back at p1" "$(framed)" p1
check "p3 is not a thumbnail any more" "$(pinstate p3)" ""
check "p4 still is" "$(pinstate p4)" PIN-SHOWN
pkill -P "$$" -x alacritty
