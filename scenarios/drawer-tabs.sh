#!/usr/bin/env bash
# Closed drawers wear a labelled tab each, in stash order, and hovering a tab (not just the sliver) opens that
# drawer, so two same-size windows on one side can be told apart and summoned individually.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
for n in A B C D; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call overview; sleep 0.8
call stashActive; sleep 1.5
call stashActive; sleep 1.5
call dump | grep -E "^DRAWER|^TAB"
check "two drawers" "$(call dump | grep -c '^DRAWER')" 2
check "each has a tab" "$(call dump | grep -c '^TAB')" 2
check "tabs are on screen" "$(call dump | grep '^TAB' | grep -c 'shown 1')" 2
/home/ivan/dev/kwin-trellis/tools/shot tabs-closed >/dev/null
first=$(call dump | grep '^DRAWER' | awk '{print $2}' | head -1)
for p in $(call dump | grep '^TAB' | awk '{print $2}'); do
  read -r tx ty tw th < <(call dump | grep "^TAB $p " | sed -E 's/.* at ([0-9.]+),([0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/')
  call simMotion 600 300 >/dev/null; sleep 1.0
  call simMotion "${tx%.*}" "${ty%.*}" >/dev/null; call simMotion $((${tx%.*} + ${tw%.*} / 2)) $((${ty%.*} + ${th%.*} / 2)) >/dev/null; sleep 1.0
  check "hovering the tab of $p opens it" "$(call dump | grep "^DRAWER $p " | grep -c ' open ')" 1
  check "and only it" "$(call dump | grep '^DRAWER' | grep -c ' open ')" 1
  /home/ivan/dev/kwin-trellis/tools/shot "tabs-open-$p" >/dev/null
done
call simMotion 600 300 >/dev/null; sleep 1.0
check "leaving closes them" "$(call dump | grep '^DRAWER' | grep -c ' open ')" 0
pkill -P "$$" -x alacritty
