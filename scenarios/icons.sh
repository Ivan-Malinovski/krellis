#!/usr/bin/env bash
# Seed scenarios/icons.rc (iconBelow=160): live windows are the default, so the icon swap is opt-in.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'pkill -P "$$" -x alacritty || true' EXIT
for n in $(seq 9); do alacritty --title "icon-$n" -e sleep 600 >/dev/null 2>&1 & sleep 0.3; done
sleep 1.5
call overview; sleep 1
call dump | tee "$NEST_OUT/icons.txt"
test "$(call dump | grep -c ' ICON')" -eq 9
$KRELLIS_ROOT/tools/shot icons
for n in 1 2; do call simButton 60 300 true 0; call simButton 60 300 false 0; done
sleep 1.2
call dump | grep "^framed 'p1'$"
! call dump | grep "^p1 .* ICON"
$KRELLIS_ROOT/tools/shot icon-focused
call overview; sleep 1
# A floating client above an icon tile owns both paint and pointer input.
call toggleDock 4; sleep 1
call simButton 600 300 true 0; call simMotion 640 320; sleep 0.2
! call dump | grep -q '^DRAG '
call simButton 640 320 false 0
$KRELLIS_ROOT/tools/shot icons-floating
call toggleDock 4; call overview; sleep 1
call simButton 60 300 true 0
call simMotion 220 320; sleep 0.3
call dump | grep '^DRAG '
call simMotion 1260 300; sleep 0.2
call simButton 1260 300 false 0; sleep 1
! call dump | grep -q '^DRAG '
test "$(call dump | grep -c ' screen ')" -eq 9
echo 'PASS icon tiles, double-click framing and pointer-driven docking'
