#!/usr/bin/env bash
# Closing the focused window focuses its neighbour in the layout: the previous one in the row, else the next.
# Run with scenarios/close-focus.rc (closeFocus=false) and EXPECT=off to see KWin's own choice is left alone.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
active() { call dump | sed -n 's/^ACTIVE //p'; }
# Panels left to right as "id" lines.
order() { call dump | sed -nE 's/^(p[0-9]+) .* screen ([0-9.]+),.*/\2 \1/p' | sort -n | awk '{print $2}'; }
# Walk focus to a panel with directional focus (it activates the window, unlike a camera-only move).
goto() {
  for _ in 1 2 3 4 5 6; do
    cur=$(active); [ "$cur" = "$1" ] && return
    a=$(order | grep -n -x "$cur" | cut -d: -f1); b=$(order | grep -n -x "$1" | cut -d: -f1)
    if [ "$b" -gt "$a" ]; then call focusDirection right; else call focusDirection left; fi; sleep 0.8
  done
}
# Click a panel's centre: focuses it directly, without passing the windows between.
click() {
  read -r x y w h < <(call dump | sed -nE "s/^$1 .* screen ([0-9.]+),([0-9.]+) ([0-9.]+)x([0-9.]+) .*/\\1 \\2 \\3 \\4/p" | head -1)
  cx=$(awk "BEGIN{print int($x + $w/2)}"); cy=$(awk "BEGIN{print int($y + $h/2)}")
  call simButton "$cx" "$cy" true 0; call simButton "$cx" "$cy" false 0; sleep 0.8
}
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
for n in A B C D; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 1.5
done
sleep 1
set -- $(order); l1=$1; l2=$2; l3=$3; l4=$4
echo "order $l1 $l2 $l3 $l4"
# Focus the third window and close it: focus goes to the second (previous sibling).
goto "$l3"
check "focused third" "$(active)" "$l3"
call closeActive; sleep 1.5
if [ "${EXPECT:-on}" = on ]; then
  check "close hands focus to the previous sibling" "$(active)" "$l2"
else
  [ "$(active)" != "$l2" ] && echo "PASS closeFocus off leaves KWin's choice ($(active))" || echo "NOTE off but active is $l2"
fi
# Close the first window after visiting the last one: KWin would return to the last one, the layout says the next one.
click "$l4"; click "$l1"
check "clicked first" "$(active)" "$l1"
call closeActive; sleep 1.5
if [ "${EXPECT:-on}" = on ]; then
  check "first window hands focus to the next one, not the last visited" "$(active)" "$l2"
else
  [ "$(active)" = "$l4" ] && echo "PASS closeFocus off: KWin returned to the last visited ($l4)" || echo "NOTE off, active is $(active)"
fi
pkill -P "$$" -x alacritty || true
