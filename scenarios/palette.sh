#!/usr/bin/env bash
# Quick-jump palette: it lists windows (most recent first, the current one last) and saved views, filters as you type,
# jumps with Return or a click, and closes with Esc or a click outside. Keys go to its field, not to the window below.
# Drives handleKey/handleButton through simKey/simText/simButton, so a global chord (Meta+/) is not covered: check it live.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
active() { call dump | sed -n 's/^ACTIVE //p'; }
pal() { call dump | sed -n 's/^PALETTE \([a-z]*\) query .*/\1/p' | head -1; }
rows() { call dump | grep -c '^PROW'; }
sel() { call dump | sed -n 's/^PROW >[0-9]* \(.*\)/\1/p' | head -1; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
ESC=16777216; RET=16777220; DOWN=16777237; UP=16777235
key() { call simKey "$1" true; call simKey "$1" false; }
for n in "alpha notes" "beta build" "gamma logs"; do
  alacritty --title "$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
check "start active" "$(active)" p3
check "closed at rest" "$(pal)" ""
echo "--- open and list"
call togglePalette; sleep 0.4
check "opens" "$(pal)" shown
check "lists every window" "$(rows)" 3
check "previous window first" "$(sel | cut -d'|' -f1 | sed 's/^[0-9]* //;s/ *$//')" "beta build"
$KRELLIS_ROOT/tools/shot palette >/dev/null
echo "--- keys stay in the palette"
call simText "alp"; sleep 0.3
check "filters" "$(rows)" 1
$KRELLIS_ROOT/tools/shot palette-typed >/dev/null
check "match is the right window" "$(sel | cut -d'|' -f1 | sed 's/^[0-9]* //;s/ *$//')" "alpha notes"
check "typing did not change focus" "$(active)" p3
key $RET; sleep 1
check "return jumps" "$(active)" p1
check "palette closes after a jump" "$(pal)" ""
echo "--- empty query goes to the previous window"
call togglePalette; sleep 0.3
key $RET; sleep 1
check "return with no query goes back" "$(active)" p3
echo "--- selection and Esc"
call togglePalette; sleep 0.3
key $DOWN; sleep 0.2
check "down moves the selection" "$(call dump | sed -n 's/^PALETTE .* selected \([0-9-]*\) of.*/\1/p')" 1
key $UP; key $UP; sleep 0.2
check "up wraps around" "$(call dump | sed -n 's/^PALETTE .* selected \([0-9-]*\) of.*/\1/p')" 2
key $ESC; sleep 0.6
check "esc closes" "$(pal)" ""
check "esc does not jump" "$(active)" p3
echo "--- nothing matches"
call togglePalette; sleep 0.3
call simText "zzzq"; sleep 0.3
check "no rows" "$(rows)" 0
key $RET; sleep 0.5
check "return with no match stays open" "$(pal)" shown
key $ESC; sleep 0.5
echo "--- toggling closes it"
call togglePalette; sleep 0.3
call togglePalette; sleep 0.5
check "the shortcut again closes" "$(pal)" ""
echo "--- saved views"
call saveView; sleep 0.3
call simText "tour"; key $RET; sleep 0.5
call togglePalette; sleep 0.3
call simText "tour"; sleep 0.3
check "a saved view is listed" "$(call dump | grep '^PROW' | grep -c '| view')" 1
key $RET; sleep 1
check "jumping to a view closes" "$(pal)" ""
echo "--- the pointer"
call togglePalette; sleep 0.3
before=$(active)
# the card sits 14% down the work area, 56px of search field, a rule, then 50px rows
call simMotion 640 244 >/dev/null; sleep 0.3
check "hover selects a row" "$(call dump | sed -n 's/^PALETTE .* selected \([0-9-]*\) of.*/\1/p')" 1
call simButton 640 244 true 0 >/dev/null; call simButton 640 244 false 0 >/dev/null; sleep 1
check "a click on a row closes it" "$(pal)" ""
[ "$(active)" != "$before" ] && echo "PASS a click on a row jumps" || echo "FAIL a click on a row jumps: still $before"
call togglePalette; sleep 0.3
call simButton 20 780 true 0 >/dev/null; call simButton 20 780 false 0 >/dev/null; sleep 0.6
check "a click outside closes" "$(pal)" ""
pkill -P "$$" -x alacritty
