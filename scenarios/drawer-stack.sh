#!/usr/bin/env bash
# Several drawers on one edge share it (each sliver is its own hover target, one slides out at a time),
# and an open drawer can be resized, staying anchored to the edge.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
drawer() { call dump | grep "^DRAWER $1 "; }
for n in A B C D; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call overview; sleep 0.8
for i in 3 2; do
  call simNativeResizeStart $i 0 0.5 | grep -qx true || echo "FAIL no native resize on $i"
  call simNativeResizeMove 900 0 >/dev/null; sleep 0.3
  call simNativeResizeEnd true >/dev/null; sleep 1.2
done
call dump | grep -E "^DRAWER|^ORDER"
check "two drawers" "$(call dump | grep -c '^DRAWER')" 2
check "two tiles left" "$(call dump | grep '^ORDER' | wc -w)" 3
/home/ivan/dev/kwin-trellis/tools/shot stack-closed >/dev/null
echo "--- hover the upper sliver"
call simMotion 1275 100 >/dev/null; sleep 1.0
drawer p3; drawer p4
check "upper drawer open" "$(drawer p3 | grep -c ' open ')" 1
check "lower drawer closed" "$(drawer p4 | grep -c ' closed ')" 1
echo "--- while it is open the other sliver does not take over"
call simMotion 1275 700 >/dev/null; sleep 0.6
check "still the upper one" "$(drawer p3 | grep -c ' open ')" 1
echo "--- leave, then hover the lower sliver"
call simMotion 300 300 >/dev/null; sleep 0.12
/home/ivan/dev/kwin-trellis/tools/shot stack-closing >/dev/null
sleep 0.9
check "all closed again" "$(call dump | grep -c '^DRAWER .* open ')" 0
call simMotion 1275 700 >/dev/null; sleep 1.0
check "lower drawer open" "$(drawer p4 | grep -c ' open ')" 1
check "upper drawer closed" "$(drawer p3 | grep -c ' closed ')" 1
/home/ivan/dev/kwin-trellis/tools/shot stack-open >/dev/null
echo "--- widen the open drawer from its inner edge"
before=$(drawer p4 | sed -n 's/.* \([0-9.]*\)x[0-9.]*$/\1/p')
call simNativeResizeStart 3 0 0.5 | grep -qx true || echo "FAIL no native resize on the drawer"
call simNativeResizeMove -200 0 >/dev/null; sleep 0.3
call simNativeResizeEnd true >/dev/null; sleep 1.2
drawer p4
after=$(drawer p4 | sed -n 's/.* \([0-9.]*\)x[0-9.]*$/\1/p')
echo "width $before -> $after"
[ "$(awk -v a="$after" -v b="$before" 'BEGIN{print (a > b + 100)}')" = 1 ] && echo "PASS wider" || echo "FAIL not wider"
call simMotion 300 300 >/dev/null; sleep 1.0
drawer p4
check "still a drawer, closed at the edge" "$(drawer p4 | grep -c "closed at 1268")" 1
pkill -P "$$" -x alacritty
