#!/usr/bin/env bash
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'pkill -P "$$" -x alacritty || true' EXIT
for n in A B; do alacritty --title "name-$n" -e sleep 600 >/dev/null 2>&1 & sleep 0.5; done
sleep 1
call toggleBar >/dev/null # the bar is off by default
sleep 0.5
save() {
    for x in $(seq 12 6 1000); do
        if [ "$(call barHit "$x" 776)" = save ]; then call simButton "$x" 776 true 0; call simButton "$x" 776 false 0; sleep 0.5; return; fi
    done
    exit 1
}
call focusIndex 0; sleep 1
save
call dump | grep '^NAMING 1 error 0$'
call simText '   '; call simKey 16777220 true; call simKey 16777220 false
call dump | grep '^NAMING 1 error 1$'
call simKey 16777216 true; call simKey 16777216 false
call dump | grep '^NAMING 0 error 0$'
! call dump | grep -q '^FRAMING '
save
call simText 'Reading Øresund'
# Backspace and replacement exercise real TextInput editing, not a setter.
call simKey 16777219 true; call simKey 16777219 false; call simText 'd'
call simKey 16777220 true; call simKey 16777220 false; sleep 0.5
call dump | tee "$NEST_OUT/saved.txt" | grep "^FRAMING .* 'Reading Øresund'$"
call dump | grep '^NAMING 0 error 0$'
/home/ivan/dev/krellis/tools/shot named-view
echo 'PASS named framing, Unicode, text editing, blank rejection and cancellation'
