#!/usr/bin/env bash
# Tabbed tiles (`tabs`): grouping windows into one tile, the strip, switching by key / click, closing by middle
# click, tearing a tab out by drag (by a tab or by the bare strip), dropping a tile on another tile's title bar to tab
# it (the middle of a tile does not tab), and splitting back out.
# Seeded with scenarios/tabs.rc. Run it with a krellisrc that has tabs off and it must FAIL (grouping is a no-op).
# Drives the coordinator through the D-Bus slots and simButton/simMotion, so the global chords are not covered.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
active() { call dump | sed -n 's/^ACTIVE //p'; }
panels() { call dump | grep -E "^[a-z0-9-]+ '.*' screen " | cut -d' ' -f1 | tr '\n' ' ' | sed 's/ $//'; }
npanels() { panels | wc -w; }
rectof() { call dump | sed -nE "s/^$1 '.*' screen ([-0-9.]+),([-0-9.]+) ([0-9.]+)x([0-9.]+) .*/\1 \2 \3 \4/p"; }
strips() { call dump | grep -c '^STRIP '; }
stowed() { call dump | grep -c '^TAB '; }
strip() { call dump | grep "^STRIP $1 "; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
click() { call simButton "$1" "$2" true 0 >/dev/null; call simButton "$1" "$2" false 0 >/dev/null; sleep 0.6; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
check "three tiles" "$(panels)" "p1 p2 p3"
check "no strip without tabs" "$(strips)" 0
echo "--- group the last window into its neighbour"
call groupLeft; sleep 1.2
check "two tiles" "$(panels)" "p1 p2"
check "one strip" "$(strips)" 1
check "one tab stowed" "$(stowed)" 1
read -r sx sy sw sh <<<"$(strip p2 | sed -nE 's/^STRIP p2 ([-0-9.]+),([-0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/p')"
echo "strip at $sx,$sy ${sw}x$sh"
check "strip is a tab bar high" "$sh" 28.0
winrow() { call dump | grep "^p2 "; }
read -r fy fh <<<"$(winrow | sed -nE 's/.* frame [-0-9.]+,([-0-9.]+) [0-9.]+x([0-9.]+).*/\1 \2/p')"
check "window sits under the strip" "$(awk -v s="$sy" -v f="$fy" 'BEGIN{print (f >= s + 28 - 1) ? "yes" : "no"}')" yes
stowedy=$(call dump | grep '^TAB ' | sed -nE 's/.* frame [-0-9.]+,([-0-9.]+) .*/\1/p')
check "stowed tab has the same place" "$stowedy" "$fy"
check "grouped window is shown" "$(winrow | grep -o "win-C")" win-C
$KRELLIS_ROOT/tools/shot tabs-grouped >/dev/null
echo "--- switch by key"
call previousTab; sleep 0.8
check "other tab shown" "$(winrow | grep -o "win-[ABC]")" win-B
check "the shown one is stowed now" "$(call dump | grep '^TAB ' | grep -o 'win-[ABC]')" win-C
call nextTab; sleep 0.8
check "back to the first" "$(winrow | grep -o "win-[ABC]")" win-C
echo "--- switch by click"
tw=$(strip p2 | sed -nE 's/.* tab ([0-9.]+) shown.*/\1/p')
cx=$(awk -v x="$sx" -v w="$tw" 'BEGIN{printf "%d", x + w/2}'); cy=$(awk -v y="$sy" 'BEGIN{printf "%d", y + 14}')
click "$cx" "$cy"
check "clicked the first tab" "$(winrow | grep -o "win-[ABC]")" win-B
cx2=$(awk -v x="$sx" -v w="$tw" 'BEGIN{printf "%d", x + w*1.5}')
click "$cx2" "$cy"
check "clicked the second tab" "$(winrow | grep -o "win-[ABC]")" win-C
echo "--- split out"
call splitTab; sleep 1.2
check "three tiles again" "$(npanels)" 3
check "strip gone" "$(strips)" 0
check "nothing stowed" "$(stowed)" 0
echo "--- middle click on a tab closes it"
call groupLeft; sleep 1.2
read -r sx sy <<<"$(strip p2 | sed -nE 's/^STRIP p2 ([-0-9.]+),([-0-9.]+) .*/\1 \2/p')"
tw=$(strip p2 | sed -nE 's/.* tab ([0-9.]+) shown.*/\1/p')
cx=$(awk -v x="$sx" -v w="$tw" 'BEGIN{printf "%d", x + w*1.5}'); cy=$(awk -v y="$sy" 'BEGIN{printf "%d", y + 14}')
call simMiddleButton "$cx" "$cy" true >/dev/null; call simMiddleButton "$cx" "$cy" false >/dev/null; sleep 1.5
check "closing the second tab leaves one tile with no strip" "$(strips)" 0
check "two tiles" "$(panels | wc -w)" 2
echo "--- tear a tab out by dragging it"
alacritty --title win-D -e sh -c "echo D; sleep 600" >/dev/null 2>&1 & sleep 2.5
check "three tiles" "$(npanels)" 3
call groupLeft; sleep 1.2
check "grouped again" "$(strips)" 1
tabbed=$(call dump | sed -nE 's/^STRIP ([^ ]+) .*/\1/p' | head -1)
read -r sx sy <<<"$(call dump | sed -nE 's/^STRIP [^ ]+ ([-0-9.]+),([-0-9.]+) .*/\1 \2/p' | head -1)"
tw=$(call dump | sed -nE 's/^STRIP .* tab ([0-9.]+) shown.*/\1/p' | head -1)
read -r ox oy ow oh <<<"$(rectof p1)"
echo "tabbed $tabbed strip $sx,$sy tab $tw other $ox,$oy ${ow}x$oh"
cx=$(awk -v x="$sx" -v w="$tw" 'BEGIN{printf "%d", x + w*1.5}'); cy=$(awk -v y="$sy" 'BEGIN{printf "%d", y + 14}')
tx=$(awk -v x="$ox" 'BEGIN{printf "%d", x + 12}'); ty=$(awk -v y="$oy" -v h="$oh" 'BEGIN{printf "%d", y + h/2}')
call simButton "$cx" "$cy" true 0 >/dev/null
for i in 1 2 3 4 5 6; do
  call simMotion "$(awk -v a="$cx" -v b="$tx" -v i=$i 'BEGIN{printf "%d", a + (b - a) * i / 6}')" "$(awk -v a="$cy" -v b="$ty" -v i=$i 'BEGIN{printf "%d", a + (b - a) * i / 6}')" >/dev/null; sleep 0.15
done
sleep 0.5
$KRELLIS_ROOT/tools/shot tabs-dragging >/dev/null
call simButton "$tx" "$ty" false 0 >/dev/null; sleep 1.5
echo "after tear-out: $(panels) strips $(strips) stowed $(stowed)" | grep -E "^(STRIP|TAB|[a-z0-9-]+ ')" | sed "s/ client.*//"
check "the tab got a tile of its own" "$(npanels)" 3
check "no strips left" "$(strips)" 0
check "nothing stowed" "$(stowed)" 0
drag_tile() { # drag tile p1 by its top to X,Y and drop it there
    read -r ox oy ow oh <<<"$(rectof p1)"
    call simDragStart 0 "$(awk -v x="$ox" -v w="$ow" 'BEGIN{printf "%d", x + w/2}')" "$(awk -v y="$oy" 'BEGIN{printf "%d", y + 10}')" >/dev/null
    call simDragMove 400 400 >/dev/null; sleep 0.3
    for i in 1 2 3 4; do call simDragMove "$(($1 - 100 + 25 * i))" "$2" >/dev/null; sleep 0.15; done
    call simDragMove "$1" "$2" >/dev/null; sleep 0.4
    call dump | grep -E "DRAG"; call simDragEnd true >/dev/null; sleep 1.5
}
echo "--- dropping on the middle of a tile does not tab it"
# Lifting the first tile closes its gap, so the other two share the screen: the right-hand one spans x 640..1274.
drag_tile 960 400
echo "after middle drop: $(panels) strips $(strips)"
check "a middle drop does not tab" "$(strips)" 0
check "the dropped tile is still a tile" "$(npanels)" 3
echo "--- right under the title band is the split-above zone"
read -r ox oy ow oh <<<"$(rectof p1)"
call simDragStart 0 "$(awk -v x="$ox" -v w="$ow" 'BEGIN{printf "%d", x + w/2}')" "$(awk -v y="$oy" 'BEGIN{printf "%d", y + 10}')" >/dev/null
call simDragMove 400 400 >/dev/null; sleep 0.3
for pt in "960 200" "960 120" "960 60"; do set -- $pt; call simDragMove $1 $2 >/dev/null; sleep 0.5; done
echo "just under the band: $(call dump | grep '^DRAG')"
check "just under the band is a split target" "$(call dump | grep '^DRAG' | grep -c 'candidate -1')" 0
call simDragMove 960 20 >/dev/null; sleep 0.5
echo "on the band: $(call dump | grep '^DRAG')"
call simDragEnd false >/dev/null; sleep 1
echo "--- dropping on the title bar of a tile tabs it"
drag_tile 960 20
echo "after title-bar drop: $(panels) strips $(strips) stowed $(stowed)"
check "dropped on the title bar became a tab" "$(strips)" 1
check "one fewer tile" "$(npanels)" 2
call dump | grep -E "^(STRIP|TAB)"
echo "--- the bare part of a strip drags the tab on show"
shown=$(call dump | grep -E "^p[0-9]+ " | grep -v '^TAB' | grep -o "win-[A-D]" | tr '\n' ' ')
read -r sx sy sw <<<"$(call dump | sed -nE 's/^STRIP [^ ]+ ([-0-9.]+),([-0-9.]+) ([0-9.]+)x.*/\1 \2 \3/p' | head -1)"
tw=$(call dump | sed -nE 's/^STRIP .* tab ([0-9.]+) shown.*/\1/p' | head -1)
bx=$(awk -v x="$sx" -v w="$sw" 'BEGIN{printf "%d", x + w - 12}'); by=$(awk -v y="$sy" 'BEGIN{printf "%d", y + 14}')
echo "strip $sx,$sy ${sw}x tab $tw; pressing the bare part at $bx,$by"
before=$(call dump | grep -E "^p[0-9]+ " | grep -o "win-[A-D]" | tr '\n' ' ')
click "$bx" "$by"
check "a click on the bare strip changes nothing" "$(call dump | grep -E "^p[0-9]+ " | grep -o "win-[A-D]" | tr '\n' ' ')" "$before"
read -r ox oy ow oh <<<"$(rectof p1)"
tx=$(awk -v x="$ox" 'BEGIN{printf "%d", x + 12}'); ty=$(awk -v y="$oy" -v h="$oh" 'BEGIN{printf "%d", y + h/2}')
call simButton "$bx" "$by" true 0 >/dev/null
for i in 1 2 3 4 5 6; do
  call simMotion "$(awk -v a="$bx" -v b="$tx" -v i=$i 'BEGIN{printf "%d", a + (b - a) * i / 6}')" "$(awk -v a="$by" -v b="$ty" -v i=$i 'BEGIN{printf "%d", a + (b - a) * i / 6}')" >/dev/null; sleep 0.15
done
sleep 0.4
check "dragging the bare strip lifts a window" "$(call dump | grep -c '^DRAG')" 1
call simButton "$tx" "$ty" false 0 >/dev/null; sleep 1.5
check "the shown tab got a tile of its own" "$(npanels)" 3
check "no strip left" "$(strips)" 0
pkill -P "$$" -x alacritty
