#!/usr/bin/env bash
# Fly-to: a long focus jump zooms out and back in (camera width grows past the final one, then settles on the target);
# a short hop does not. Seed scenarios/flyto.rc (glide=true, flyTo=true); the second half turns flyTo off.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
camw() { call dump | sed -n 's/^camera [-0-9.e]* [-0-9.e]* \([-0-9.e]*\) .*/\1/p'; }
sample() { # name: sample the camera width every 80 ms for 1.6 s, print min/max
  local lo=999 hi=0 w
  for i in $(seq 1 20); do w=$(camw); lo=$(awk "BEGIN{print ($w<$lo)?$w:$lo}"); hi=$(awk "BEGIN{print ($w>$hi)?$w:$hi}"); sleep 0.08; done
  echo "$1 min=$lo max=$hi final=$(camw)"; echo "$lo $hi" >"$NEST_OUT/$1.mm"
}
for n in 1 2 3 4 5 6 7 8; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 1.6
done
sleep 2
call dump | grep -E "^camera"
call focusIndex 0; sleep 2.5
echo "--- far jump (first -> last), flyTo on"
call focusIndex 7 & pid=$!; sample far-on; wait $pid
sleep 1
echo "--- short hop"
call focusIndex 6 & pid=$!; sample near-on; wait $pid
sleep 1
echo "--- flyTo off"
printf '[General]\nglide=true\nmap=false\nflyTo=false\nfollowTheme=false\ntoasts=false\nwhichKey=false\n' >"$NEST_WORK/config/krellisrc"
call reloadSettings
call focusIndex 0; sleep 2.5
call focusIndex 7 & pid=$!; sample far-off; wait $pid
verdict() { # name expect(fly|still)
  read -r lo hi <"$NEST_OUT/$1.mm"
  if [ "$2" = fly ]; then awk "BEGIN{exit !($hi > $lo * 2)}" && echo "PASS $1 zooms out" || echo "FAIL $1: expected a zoom-out ($lo..$hi)"
  else awk "BEGIN{exit !($hi < $lo * 1.2)}" && echo "PASS $1 stays put" || echo "FAIL $1: unexpected zoom-out ($lo..$hi)"; fi
}
verdict far-on fly; verdict near-on still; verdict far-off still
pkill -P "$$" -x alacritty
