#!/usr/bin/env bash
# Scratch layer: windows marked as scratch come and go together, dim the rest while up, dismiss on outside focus or click, and are driven from the minimap.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
active() { call dump | sed -n 's/^ACTIVE //p'; }
scratch() { call dump | sed -n 's/^SCRATCH count \([0-9]*\) shown \([0-9]\).*/\1 \2/p' | head -1; }
tiles() { call dump | grep -c "^p[0-9]"; }
floats() { call dump | grep -E "^p[0-9]" | grep -c " FLOAT"; }
dim() { call dump | sed -n 's/^LAYER dim //p' | head -1; }
isactivescratch() { call dump | grep -c "^SCRATCHWIN $(active) shown"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
check "no scratch yet" "$(scratch)" "0 0"
echo "--- off by default"
check "scratchpad is off" "$(call dump | sed -n 's/^SCRATCH .* enabled \([01]\).*/\1/p' | head -1)" 0
call toggleScratchMarkActive; sleep 0.6
check "off: marking does nothing" "$(scratch)" "0 0"
check "off: nothing floated" "$(floats)" 0
printf '[General]\nglide=true\nmap=false\nscratchpad=true\n' >"$XDG_CONFIG_HOME/krellisrc"
call reloadSettings; sleep 1
check "scratchpad is on" "$(call dump | sed -n 's/^SCRATCH .* enabled \([01]\).*/\1/p' | head -1)" 1
call toggleScratch ""; sleep 0.5; check "toggle with none is a no-op" "$(scratch)" "0 0"
check "active before mark" "$(active)" p3
call toggleScratchMarkActive; sleep 1.2
check "first marked" "$(scratch)" "1 1"
check "layer dims the rest" "$(dim)" "1.00"
call nextPanel; sleep 1.2
check "focus on a tiled window dismisses the layer" "$(scratch)" "1 0"
check "the dim is gone" "$(dim)" "0.00"
call nextPanel; sleep 0.6
call toggleScratchMarkActive; sleep 1.2
check "second marked" "$(scratch)" "2 1"
/home/ivan/dev/krellis/tools/shot scratch-layer >/dev/null
call toggleScratch ""; sleep 1.2
check "toggle hides the whole layer" "$(scratch)" "2 0"
check "hidden scratch windows are not in the dock" "$(call dump | grep -c 'in Dock')" 0
call toggleScratch ""; sleep 1.2
check "toggle brings every window back" "$(scratch)" "2 1"
check "both shown" "$(call dump | grep -c '^SCRATCHWIN p[0-9]* shown')" 2
check "both float" "$(floats)" 2
check "a scratch window has the focus" "$(isactivescratch)" 1
check "layer dims again" "$(dim)" "1.00"
sleep 1.5; /home/ivan/dev/krellis/tools/shot scratch-both >/dev/null
call nextPanel; sleep 0.6; check "cycling skips scratch windows" "$([ "$(isactivescratch)" = 0 ] && echo ok)" ok
check "outside focus dismissed the layer" "$(scratch)" "2 0"
echo "--- click outside"
call toggleScratch ""; sleep 1.2
call simMotion 80 400; sleep 0.3
call simButton 80 400 true 0; call simButton 80 400 false 0; sleep 1.2
check "a click on a tiled window dismisses the layer" "$(scratch)" "2 0"
echo "--- minimap"
call toggleMap >/dev/null; sleep 1.2
read -r gx gy gw gh < <(call dump | grep -E "^MAP" | sed -E 's/.*geometry ([0-9]+),([0-9]+) ([0-9]+)x([0-9]+).*/\1 \2 \3 \4/')
call simMotion $((gx + gw - 8)) $((gy + gh - 30)); sleep 0.8
px=0; py=0
for y in $(seq $((gy + gh - 40)) 3 $((gy + gh - 8))); do
  for x in $((gx + gw - 200)) $((gx + gw - 170)) $((gx + gw - 150)); do
    if [ "$(call barHit $x $y)" = "map:scratch" ]; then px=$x; py=$y; break 2; fi
  done
done
check "minimap exposes a scratchpad button" "$([ $px != 0 ] && echo found)" found
/home/ivan/dev/krellis/tools/shot scratch-map >/dev/null
call simButton $px $py true 0; call simButton $px $py false 0; sleep 1.2
check "minimap button shows the layer" "$(scratch)" "2 1"
call simMotion $px $py; call simButton $px $py true 0; call simButton $px $py false 0; sleep 1.2
check "minimap button hides it" "$(scratch)" "2 0"
echo "--- unmark"
call toggleScratch ""; sleep 1.2; call toggleScratchMarkActive; sleep 0.8
check "unmarked one" "$(scratch | cut -d' ' -f1)" "1"
echo "--- switched off with the layer up"
check "layer still up" "$(scratch)" "1 1"
printf '[General]\nglide=true\nmap=false\n' >"$XDG_CONFIG_HOME/krellisrc"
call reloadSettings; sleep 1.5
check "off: no scratch windows" "$(scratch)" "0 0"
check "off: dim is gone" "$(dim)" "0.00"
call toggleScratch ""; sleep 0.5
check "off: the shortcut slot does nothing" "$(scratch)" "0 0"
pkill -P "$$" -x alacritty
