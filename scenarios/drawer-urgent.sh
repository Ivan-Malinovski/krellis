#!/usr/bin/env bash
# A window behind a closed edge drawer cannot show the amber outline its tile would carry, so its tab pulses with the
# accent while the window asks for attention (KWin's "demands attention"), and stops when the flag clears, when the
# drawer opens or when urgent handling is off. The pulse runs on the effect's frame clock, not in QML (the tab scene is
# offscreen, so an animation inside it would never repaint), which is why the dump reports it: samples a moment apart
# must differ while it blinks and match when it does not.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
urgentpanel() { call dump | grep '^TAB' | grep 'urgent 1' | awk '{print $2}' | head -1; }
blinkof() { call dump | grep "^TAB $1 " | sed -nE 's/.* blink ([0-9.]+).*/\1/p' | head -1; }
tabline() { call dump | grep "^TAB $1 " | head -1; }
setattn() {
cat >"$NEST_WORK/attn.js" <<JS
for (const w of workspace.windowList()) if (w.caption === '$1') w.demandsAttention = $2;
JS
n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/attn.js" attn$RANDOM)
qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null
}
# Three samples a moment apart: "yes" only if the value is actually being repainted rather than frozen. A single
# sample can catch a frame where the tab list is momentarily empty, so an empty sample is not a distinct value.
moving() { : > "$NEST_WORK/b"
           for i in 1 2 3; do
             call dump | grep "^TAB $1 " | sed -nE 's/.* blink ([0-9.]+).*/\1/p' >> "$NEST_WORK/b"
             [ $i -lt 3 ] && sleep 0.23
           done
           [ "$(grep . "$NEST_WORK/b" | sort -u | wc -l)" -gt 1 ] && echo yes || echo no; }

# Three windows, one stashed twice: the one left over keeps the keyboard, so the second stash does not fall back
# onto the drawer window (which would put it back among the tiles instead).
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call overview; sleep 0.8
call stashActive; sleep 1.5
call stashActive; sleep 1.5
check "two closed drawers" "$(call dump | grep -c '^DRAWER')" 2
check "one tab each" "$(call dump | grep -c '^TAB')" 2
other=$(call dump | grep '^TAB' | awk '{print $2}' | tail -1)

echo "--- nothing asks for attention: the tabs rest"
check "no tab is urgent" "$(call dump | grep '^TAB' | grep -c 'urgent 1')" 0
check "none is pulsing" "$(call dump | grep '^TAB' | grep -c 'blink 0.00')" 2
r1=$(tabline "$other"); sleep 0.5; r2=$(tabline "$other")
check "an idle tab asks for no frames" "$r1" "$r2"

echo "--- the hidden window asks for attention"
stashed=""
for cap in win-A win-B win-C; do
  setattn "$cap" true; sleep 0.6
  if [ -n "$(urgentpanel)" ]; then stashed=$cap; break; fi
  setattn "$cap" false
done
check "one stashed window found" "$([ -n "$stashed" ] && echo yes)" yes
p=$(urgentpanel)
check "its drawer's tab is urgent" "$([ -n "$p" ] && echo yes)" yes
check "exactly one tab pulses" "$(call dump | grep '^TAB' | grep -c 'urgent 1')" 1
check "the pulse moves between frames" "$(moving "$p")" yes
idle=$(call dump | grep '^TAB' | grep 'urgent 0' | awk '{print $2}' | head -1)
check "the other tab stays at rest" "$(blinkof "$idle")" 0.00
check "and does not ask for frames" "$(tabline "$idle")" "$(tabline "$idle")"
$KRELLIS_ROOT/tools/shot drawer-urgent >/dev/null 2>&1 || true

echo "--- clearing the flag stops the pulse"
setattn "$stashed" false; sleep 1.0
check "the tab is no longer urgent" "$(call dump | grep "^TAB $p " | grep -c 'urgent 0')" 1
check "and it rests again" "$(blinkof "$p")" 0.00
check "and stops asking for frames" "$(moving "$p")" no

echo "--- an open drawer shows the window itself, so there is no tab to pulse"
setattn "$stashed" true; sleep 0.6
read -r tx ty tw th < <(tabline "$p" | sed -E 's/.* at ([0-9.]+),([0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/')
call simMotion 600 300 >/dev/null; sleep 1.0
call simMotion ${tx%.*} ${ty%.*} >/dev/null
call simMotion $((${tx%.*} + ${tw%.*} / 2)) $((${ty%.*} + ${th%.*} / 2)) >/dev/null
sleep 1.2
check "hovering the tab opens the drawer" "$(call dump | grep "^DRAWER $p " | grep -c ' open ')" 1
# The dump keeps reporting each closed drawer's tab geometry; "shown" is what says whether anything is painted.
check "no tab is painted while one is open" "$(call dump | grep -c '^TAB.*shown 1')" 0
check "nothing is left pulsing" "$(call dump | grep -c 'blink [1-9]')" 0

echo "--- closing it brings the tab back, still asking"
call simMotion 600 300 >/dev/null; sleep 1.5
check "both tabs painted again" "$(call dump | grep -c '^TAB.*shown 1')" 2
check "the flag survived the open drawer" "$(call dump | grep "^TAB $p " | grep -c 'urgent 1')" 1
check "and the pulse resumes" "$(moving "$p")" yes

pkill -P "$$" -x alacritty
