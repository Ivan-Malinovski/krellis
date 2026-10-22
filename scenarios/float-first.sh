# Floating-first mode: new windows open floating, a drop anywhere floats, and only screen edges and corners tile.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
rc=0
ok() { echo "ok $1"; }
bad() { echo "FAIL $1"; rc=1; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
floats() { call dump | grep -c ' FLOAT$'; }
call setFloatFirst true
[ "$(call floatFirst)" = true ] && ok "mode on" || bad "mode on"
grep -q 'floatFirst=true' "$NEST_WORK/config/krellisrc" && ok "mode persisted" || bad "mode persisted"
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 1.5; done
sleep 1
[ "$(floats)" = 3 ] && ok "new windows float ($(floats))" || bad "new windows float: $(floats) floats, order '$(order)'"
call dump | grep FLOAT
drag() {  # index fromX fromY toX toY
    call simDragStart "$1" "$2" "$3" | grep -q true || { bad "could not grab $1"; return; }
    for i in 1 2 3 4; do call simDragMove "$4" "$5"; sleep 0.05; done
    sleep 0.5; LASTDRAG=$(call dump | grep -E "^DRAG"); echo "$LASTDRAG"
    call simDragEnd true; sleep 1.3
}
drag 1 640 300 640 420
[ "$(floats)" = 3 ] && ok "mid-screen drop floats" || bad "mid-screen drop: $(floats) floats, order '$(order)'"
# The whole float stays inside the area, even dropped low where it would hang off the bottom.
drag 1 640 300 500 760
# The card hovering for the drop is clamped as well.
echo "$LASTDRAG" | awk '{ split($3,p,","); split($4,s,"x"); exit !(p[2]+s[2] <= 800.5) }' && ok "dragged card stays above the panel area" || bad "dragged card hangs below the area: $LASTDRAG"
call dump | grep "win-B"
call dump | grep "win-B" | awk '{ for (i=1;i<=NF;i++) if ($i=="frame") { split($(i+1),p,","); split($(i+2),s,"x"); b=p[2]+s[2]; print b } }' | while read b; do
    awk -v b="$b" 'BEGIN { exit !(b <= 800.5) }' && echo "ok float bottom $b within 800" || { echo "FAIL float bottom $b past 800"; exit 1; }
done || rc=1
# Nothing is tiled yet, so an edge gives half the screen (floating), not the whole of it.
drag 0 640 300 8 400
[ "$(order | wc -w)" = 0 ] && [ "$(floats)" = 3 ] && ok "left edge with no tiles floats" || bad "left edge, no tiles: order '$(order)' floats $(floats)"
echo "$(call dump | grep "win-A" | cut -c1-80)"
call dump | grep "win-A" | grep -q 'screen 0.0,0.0 640.0x800.0' && ok "left edge takes the left half" || bad "left half"
drag 0 300 300 1272 790
call dump | grep "win-A" | grep -q 'screen 640.0,400.0 640.0x400.0' && ok "bottom-right corner takes a quarter" || bad "corner quarter"
# With one tile, an edge docks across the whole layout.
call toggleFloat 0
[ "$(order | wc -w)" = 1 ] && ok "one tile: $(order)" || bad "one tile: order '$(order)' floats $(floats)"
drag 1 640 300 8 400
[ "$(order | wc -w)" = 2 ] && [ "$(floats)" = 1 ] && ok "left edge docks: $(order)" || bad "left edge: order '$(order)' floats $(floats)"
# A corner splits the tile there.
drag 2 640 300 10 15
[ "$(order | wc -w)" = 3 ] && [ "$(floats)" = 0 ] && ok "corner docks: $(order)" || bad "corner: order '$(order)' floats $(floats)"
# Pulling a tile out and dropping it mid-screen floats it.
drag 0 100 100 640 420
[ "$(order | wc -w)" = 2 ] && [ "$(floats)" = 1 ] && ok "tile dropped mid-screen floats" || bad "tile out: order '$(order)' floats $(floats)"
call setFloatFirst false
[ "$(call floatFirst)" = false ] && ok "mode off" || bad "mode off"
pkill -P "$$" -x alacritty
exit $rc
