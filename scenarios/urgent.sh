#!/usr/bin/env bash
# Urgent windows: a window that is not active and demands attention gets an outline (URGENT dump line, fade -> 1.00),
# is listed first in the palette with "needs attention", and Meta+U's action (focusUrgent) jumps to it. Activating the
# window makes KWin clear the flag, which ends all of it. With urgent=false none of it applies.
# Seeded with scenarios/urgent.rc. The global chord (Meta+U) is not covered; check it live.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
active() { call dump | sed -n 's/^ACTIVE //p'; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
urgent() { call dump | sed -nE 's/^URGENT (p[0-9]+) fade ([0-9.]+).*/\1:\2/p' | tr '\n' ' ' | sed 's/ $//'; }
mapurgent() { call dump | grep -o 'MAPURGENT [0-9]*' | head -1; }
setattn() {
cat >"$NEST_WORK/attn.js" <<JS
for (const w of workspace.windowList()) if (w.caption === '$1') w.demandsAttention = $2;
JS
n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/attn.js" attn$RANDOM)
qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null
}
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
check "starts with the last window active" "$(active)" p3
check "nothing urgent at first" "$(urgent)" ""
echo "--- a background window asks for attention"
setattn win-A true; sleep 1
check "p1 is urgent and outlined" "$(urgent)" "p1:1.00"
call dump | grep -E "^MAP|^PALETTE" | head -2
$KRELLIS_ROOT/tools/shot urgent >/dev/null 2>&1 || true
echo "--- the active window cannot be urgent"
setattn win-C true; sleep 0.6
check "active window ignored" "$(urgent)" "p1:1.00"
echo "--- jump to it"
call focusUrgent; sleep 1.6
check "focusUrgent activates p1" "$(active)" p1
check "KWin cleared the flag, so no outline" "$(urgent)" ""
echo "--- nothing urgent: focusUrgent leaves focus alone"
call focusUrgent; sleep 0.8
check "still p1" "$(active)" p1
echo "--- palette lists the urgent window first"
setattn win-B true; sleep 1
check "p2 urgent" "$(urgent)" "p2:1.00"
call togglePalette; sleep 0.8
first=$(call dump | grep -E "^PROW" | head -1)
echo "$first"
echo "$first" | grep -q "win-B" && echo "PASS palette lists win-B first" || echo "FAIL palette first row: $first"
echo "$first" | grep -q "needs attention" && echo "PASS row says needs attention" || echo "FAIL row lacks needs attention: $first"
call togglePalette; sleep 0.5
echo "--- turning it off"
cat >"$NEST_WORK/off.rc" <<RC
[General]
glide=true
urgent=false
map=true
followTheme=false
toasts=false
whichKey=false
spawnTarget=end
peek=0
RC
cp "$NEST_WORK/off.rc" "$XDG_CONFIG_HOME/krellisrc"
call reloadSettings; sleep 1
check "no outline when disabled" "$(urgent)" ""
pkill -P "$$" -x alacritty
