#!/usr/bin/env bash
# Keyboard navigation: directional focus, move and resize mode, driven over D-Bus.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
active() { call dump | sed -n 's/^ACTIVE //p'; }
xof() { call dump | sed -n "s/^$1 .* screen \([0-9.]*\),.*/\1/p" | head -1; }
wof() { call dump | sed -n "s/^$1 .* screen [0-9.]*,[0-9.]* \([0-9.]*\)x.*/\1/p" | head -1; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
# Windows tile left to right in the order they were added unless the dump says otherwise; p3 is active and in the middle.
check "start active" "$(active)" p3
call focusDirection left; sleep 1;  check "focus left" "$(active)" p1
call focusDirection left; sleep 0.5; check "focus left at edge stays" "$(active)" p1
call focusDirection right; sleep 1; check "focus right" "$(active)" p3
call focusDirection up; sleep 0.5;  check "focus up with nothing above stays" "$(active)" p3
call moveDirection left; sleep 1.2
check "move left keeps focus" "$(active)" p3
check "move left puts it first" "$(xof p3)" 6.0
# p3 now sits in a stacked column: the first press pops it out to the left edge, the next has nowhere to go.
call moveDirection left; sleep 1.5
before=$(wof p3)
call moveDirection left; sleep 1.5
after=$(wof p3)
awk "BEGIN{exit !($after < $before * 0.9 && $after > $before * 0.7)}" && echo "PASS move into the edge shrinks by about 20%" || echo "FAIL move into the edge shrinks by about 20%: $before -> $after"
w=$(wof p3)
call resizeStep right 0.1; sleep 0.6
grown=$(wof p3)
awk "BEGIN{exit !($grown > $w)}" && echo "PASS resize right grows" || echo "FAIL resize right grows: $w -> $grown"
call toggleResizeMode; check "resize mode on" "$(call resizeModeActive)" true
sleep 0.5; $KRELLIS_ROOT/tools/shot key-nav-resize >/dev/null
call simKey 16777236 true; call simKey 16777236 false; sleep 0.6   # Right
again=$(wof p3)
awk "BEGIN{exit !($again > $grown)}" && echo "PASS resize mode arrow grows" || echo "FAIL resize mode arrow grows: $grown -> $again"
check "no stray panel from the hint" "$(call dump | grep -c "^p[0-9]")" 3
call simKey 16777216 true; call simKey 16777216 false; check "escape leaves resize mode" "$(call resizeModeActive)" false
pkill -P "$$" -x alacritty
