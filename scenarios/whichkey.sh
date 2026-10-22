#!/usr/bin/env bash
# Which-key sheet: holding Meta+Alt past the delay shows the cheat sheet; another key, a release, Shift or a click hides it;
# a quick tap shows nothing; a held chord does not re-trigger; whichKey=false disables it. Seed scenarios/whichkey.rc (delay 400).
# Drives handleKey through simKey, so it covers the tracker and the timer, not the order of the compositor's input filters
# or a global chord (Meta+Alt+Up) dismissing the sheet from the action side: check those live.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
sheet() { call dump | sed -n 's/^WHICHKEY \([a-z]*\) .*/\1/p' | head -1; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
META=16777250; ALT=16777251; SHIFT=16777248; KEY_A=65
press() { call simKey "$1" true; }
release() { call simKey "$1" false; }
for n in A B; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
check "nothing at rest" "$(sheet)" ""
echo "--- a quick tap"
press $META; press $ALT; sleep 0.15; release $ALT; release $META; sleep 0.7
check "tap shows nothing" "$(sheet)" ""
echo "--- hold"
press $META; press $ALT; sleep 0.2
check "not yet before the delay" "$(sheet)" ""
sleep 0.5
check "shown after the delay" "$(sheet)" shown
/home/ivan/dev/krellis/tools/shot whichkey >/dev/null
call dump | grep WHICHKEY
press $KEY_A; sleep 0.5
check "a key hides it" "$(sheet)" ""
sleep 0.7
check "a held chord does not re-trigger" "$(sheet)" ""
release $KEY_A; release $ALT; release $META; sleep 0.2
echo "--- release hides"
press $META; press $ALT; sleep 0.7
check "shown again on a fresh hold" "$(sheet)" shown
release $ALT; sleep 0.5
check "releasing Alt hides it" "$(sheet)" ""
release $META
echo "--- Shift joins the chord"
press $META; press $ALT; press $SHIFT; sleep 0.7
check "Meta+Alt+Shift shows nothing" "$(sheet)" ""
release $SHIFT; sleep 0.7
check "releasing Shift does not start a hold" "$(sheet)" ""
release $ALT; release $META
echo "--- a click hides it"
press $META; press $ALT; sleep 0.7
call simButton 400 400 true 0 >/dev/null; call simButton 400 400 false 0 >/dev/null; sleep 0.5
check "a button press hides it" "$(sheet)" ""
release $ALT; release $META
echo "--- whichKey=false"
printf '[General]\nglide=true\nmap=false\nfollowTheme=false\ntoasts=false\nwhichKey=false\n' >"$NEST_WORK/config/krellisrc"
call reloadSettings
press $META; press $ALT; sleep 0.8
check "disabled shows nothing" "$(sheet)" ""
release $ALT; release $META
pkill -P "$$" -x alacritty
