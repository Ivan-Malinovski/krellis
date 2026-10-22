#!/usr/bin/env bash
# Run with NEST_OUTPUTS=2. Meta+Arrow on a tile already at the edge of its output hands it to the output beside it,
# docked along the near edge; the same key back returns it.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
count() { call dumpOutput "$1" | grep -c ' screen ' || true; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
# Park everything on output 0 so the starting point does not depend on where KWin opened the windows.
for i in 0 1; do
  for w in $(seq 2 -1 0); do call selectOutput 1; call moveToOutput "$w" 0 2>/dev/null; done
done
call selectOutput -1
sleep 1
check "start: three on output 0" "$(count 0)" 3
active=$(call dump | sed -n 's/^ACTIVE //p'); echo "active $active"
# The active tile may sit mid-row: walk it to the right edge; the press after that crosses over.
for i in 1 2 3; do call moveDirection right; sleep 1.2; [ "$(count 1)" -gt 0 ] && break; done
sleep 0.5
check "edge move hands the window over (out0)" "$(count 0)" 2
check "edge move hands the window over (out1)" "$(count 1)" 1
check "window stays active" "$(call dumpOutput 1 | sed -n 's/^ACTIVE //p' | head -1)" "$(call dumpOutput 1 | grep -o '^p[0-9]*' | head -1)" 
call dumpOutput 1 | grep " screen " | head -2
call moveDirection left; sleep 1.5
check "moving back returns it (out0)" "$(count 0)" 3
check "moving back returns it (out1)" "$(count 1)" 0
call moveDirection left; sleep 1.2
check "away from any output nothing changes" "$(count 0)" 3
pkill -P "$$" -x alacritty
