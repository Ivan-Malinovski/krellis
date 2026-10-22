# Float-first leaves an empty slot where a tile was pulled out; tiling mode still reflows.
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
vacant() { call dump | grep -c '^VACANT'; }
screen() { call dump | grep "$1" | grep -o 'screen [0-9.,-]* [0-9.]*x[0-9.]*' | head -1; }
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 1.5; done
sleep 1
call dump | grep -E "win-|ORDER"
a0=$(screen win-A); b0=$(screen win-B); c0=$(screen win-C)
[ "$(order | wc -w)" = 3 ] && ok "three tiles" || bad "three tiles: '$(order)'"

# Tiling: pulling a tile out reflows the neighbours into its space.
call toggleFloat 1; sleep 1.2
[ "$(vacant)" = 0 ] && ok "tiling leaves no vacancy" || bad "tiling vacancy: $(vacant)"
[ "$(screen win-C)" != "$c0" ] && ok "tiling reflows C" || bad "tiling kept C at $c0"
call toggleFloat 1; sleep 1.2
[ "$(floats)" = 0 ] && [ "$(order | wc -w)" = 3 ] && ok "docked back: $(order)" || bad "dock back: order '$(order)' floats $(floats)"

# Float-first: the neighbours stay, the slot stays empty.
call setFloatFirst true
a0=$(screen win-A); b0=$(screen win-B); c0=$(screen win-C)
call toggleFloat 1; sleep 1.2
call dump | grep -E "win-|ORDER|VACANT|FLOAT"
[ "$(vacant)" = 1 ] && ok "one vacancy" || bad "vacancies: $(vacant)"
[ "$(floats)" = 1 ] && ok "B floats" || bad "floats: $(floats)"
[ "$(screen win-A)" = "$a0" ] && [ "$(screen win-C)" = "$c0" ] && ok "neighbours stay put" || bad "A '$(screen win-A)' was '$a0', C '$(screen win-C)' was '$c0'"
call dump | grep '^VACANT' | head -1 | grep -qF "$(echo "$b0" | sed 's/screen //; s/ /\n/' | head -1) $(echo "$b0" | awk '{ print $3 }')" && ok "vacancy occupies B's old slot" || bad "vacancy rect: $(call dump | grep '^VACANT') vs $b0"
[ "$(order | wc -w)" = 2 ] && ok "order skips the vacancy: $(order)" || bad "order '$(order)'"

# Docking the float back fills the slot.
call toggleFloat 1; sleep 1.2
[ "$(vacant)" = 0 ] && [ "$(floats)" = 0 ] && ok "dock back fills the slot" || bad "fill: vacant $(vacant) floats $(floats)"
[ "$(screen win-B)" = "$b0" ] && ok "B is back where it was" || bad "B '$(screen win-B)' was '$b0'"

# A new window takes a vacancy rather than splitting a tile (tiling placement is forced with a rule-free dock).
call toggleFloat 0; sleep 1.0
[ "$(vacant)" = 1 ] && ok "A vacated" || bad "A vacated: $(vacant)"
alacritty --title "win-D" -e sleep 600 >/dev/null 2>&1 & sleep 2
[ "$(floats)" = 2 ] && ok "float-first opens D floating" || bad "D floats: $(floats)"
call toggleFloat 3; sleep 1.2
[ "$(vacant)" = 0 ] && ok "D docks into the slot" || bad "D slot: $(vacant)"
[ "$(screen win-D)" = "$a0" ] && ok "D sits at A's old rect" || bad "D '$(screen win-D)' was '$a0'"

# Dragging a float onto a vacancy fills it.
c1=$(screen win-C)
call toggleFloat 2; sleep 1.0
[ "$(vacant)" = 1 ] && ok "C vacated" || bad "C vacated: $(vacant)"
vx=$(call dump | grep '^VACANT' | head -1 | awk '{ split($3,p,","); split($4,s,"x"); printf "%d %d", p[1]+s[1]/2, p[2]+s[2]/2 }')
echo "vacancy centre: $vx"
call simDragStart 0 640 300 | grep -q true && ok grab || bad grab
set -- $vx
for i in 1 2 3 4; do call simDragMove "$1" "$2"; sleep 0.05; done
sleep 0.5; call dump | grep -E "^(DRAG|SLOT)"
call simDragEnd true; sleep 1.3
call dump | grep -E "win-|ORDER|VACANT|FLOAT"
[ "$(vacant)" = 0 ] && ok "dropping on the vacancy fills it" || bad "drop fill: vacant $(vacant)"
[ "$(floats)" = 1 ] && ok "only C floats" || bad "floats $(floats)"
[ "$(screen win-A)" = "$c1" ] && ok "A took C's slot" || bad "A '$(screen win-A)' want '$c1'"

# Pulling a tile out with the pointer leaves the same hole, and the other tiles keep their rects.
drag() {  # index fromX fromY toX toY
    call simDragStart "$1" "$2" "$3" | grep -q true || { bad "could not grab $1"; return; }
    for i in 1 2 3 4; do call simDragMove "$4" "$5"; sleep 0.05; done
    sleep 0.5; call simDragEnd true; sleep 1.3
}
d0=$(screen win-D); a1=$(screen win-A)
drag 1 1000 300 640 420
[ "$(vacant)" = 1 ] && [ "$(floats)" = 2 ] && ok "dragged tile floats and leaves a hole" || bad "drag out: vacant $(vacant) floats $(floats)"
[ "$(screen win-D)" = "$d0" ] && [ "$(screen win-A)" = "$a1" ] && ok "tiles stay put after a drag out" || bad "D '$(screen win-D)' A '$(screen win-A)'"
[ "$(order | wc -w)" = 2 ] && ok "order $(order)" || bad "order '$(order)'"
# Back to tiling: a pulled-out tile reflows the rest.
call setFloatFirst false
call toggleFloat 3; sleep 1.0
call dump | grep -E "win-|VACANT"
# Removing some other tile (which reflows) must not swallow the hole.
[ "$(vacant)" = 1 ] && ok "hole survives an unrelated reflow" || bad "hole lost: vacant $(vacant)"
pkill -P "$$" -x alacritty
exit $rc
