#!/usr/bin/env bash
# Action toasts: toggling the bar shows one, it expires, a repeat restarts the timer, a different text replaces it,
# and with toasts off nothing shows. Seed: NEST_KRELLISRC with toasts=true (scenarios/toast.rc).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
echo "--- no toast at rest"; call dump | grep -E "^TOAST" || echo "none"
echo "--- toggle bar"; call toggleBar >/dev/null; sleep 0.5; call dump | grep -E "^TOAST"
$KRELLIS_ROOT/tools/shot toast >/dev/null
echo "--- toggle again 0.5 s later: replaced by the new text"; call toggleBar >/dev/null; sleep 0.4; call dump | grep -E "^TOAST"
echo "--- expires"; sleep 2.2; call dump | grep -E "^TOAST" || echo "gone"
echo "--- toasts off"
printf '[General]\nglide=true\nmap=false\ntoasts=false\n' >"$NEST_WORK/config/krellisrc"
call reloadSettings; call toggleBar >/dev/null; sleep 0.5; call dump | grep -E "^TOAST" || echo "none (off)"
pkill -P "$$" -x alacritty
