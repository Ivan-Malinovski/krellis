#!/usr/bin/env bash
# stashActive slides the active window into its nearest edge as a drawer (again: docks it back), and the
# minimap draws a closed drawer as its sliver, not the size it opens to.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call overview; sleep 0.8
call dump | head -12
echo "--- stash the active window (the newest, right-most)"
call stashActive; sleep 1.5
call dump | grep -E "^DRAWER|^ORDER"
check "one drawer" "$(call dump | grep -c '^DRAWER')" 1
check "it slid out of a side" "$(call dump | grep '^DRAWER' | grep -c 'edge [01] ')" 1
check "two tiles left" "$(call dump | grep '^ORDER' | wc -w)" 3
echo "--- the active window moved on; stash that one too, then restore the first"
call stashActive; sleep 1.5
check "two drawers" "$(call dump | grep -c '^DRAWER')" 2
check "all drawers on sides" "$(call dump | grep '^DRAWER' | grep -vc 'edge [01] ')" 0
check "one tile stays" "$(call dump | grep '^ORDER' | wc -w)" 2
call toggleBar >/dev/null; sleep 0.5
call simButton 1230 776 true 0 >/dev/null; call simButton 1230 776 false 0 >/dev/null; sleep 1.2
read -r gx gy gw gh < <(call dump | grep -E "^MAP" | sed -E 's/.*geometry ([0-9]+),([0-9]+) ([0-9]+)x([0-9]+).*/\1 \2 \3 \4/')
sleep 2.5
call simMotion $((gx + gw - 8)) $((gy + gh - 30)) >/dev/null; sleep 1.0
/home/ivan/dev/kwin-trellis/tools/shot stash-two >/dev/null
call simMotion 600 300 >/dev/null; sleep 0.5
for _ in 1 2 3; do call dump | grep -q "^ACTIVE p3" && break; call nextPanel; sleep 0.5; done
call dump | grep "^ACTIVE"
call stashActive; sleep 1.5
call dump | grep -E "^DRAWER|^ORDER"
check "pressing it on a drawer docks it back" "$(call dump | grep -c '^DRAWER')" 1
pkill -P "$$" -x alacritty
