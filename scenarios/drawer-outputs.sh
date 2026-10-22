#!/usr/bin/env bash
# Needs NEST_OUTPUTS=2 (side by side). A drawer never sits on the edge that joins the neighbouring display:
# the stash shortcut takes the free side instead, and squeezing a tile against the joined edge makes no drawer.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
for n in A B C D; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call overview; sleep 0.8
call dump | grep -E "^OUTPUT|^ORDER|^ACTIVE|^area" | head
echo "--- the shortcut on a window that sits towards the joined side"
call stashActive; sleep 1.5
call dump | grep -E "^DRAWER"
check "one drawer" "$(call dump | grep -c '^DRAWER')" 1
check "on the free side of this display" "$(call dump | grep '^DRAWER' | grep -c 'edge [0] ')" 1
echo "--- squeeze a tile against the joined edge"
n=$(call dump | grep '^ORDER' | wc -w)
call simNativeResizeStart $((n - 2)) 0 0.5 | grep -qx true || echo "FAIL no native resize"
call simNativeResizeMove 1500 0 >/dev/null; sleep 0.8
check "no preview on the joined edge" "$(call dump | grep -c '^SQUEEZE')" 0
call simNativeResizeEnd true >/dev/null; sleep 1.5
check "no new drawer from it" "$(call dump | grep -c '^DRAWER')" 1
pkill -P "$$" -x alacritty
