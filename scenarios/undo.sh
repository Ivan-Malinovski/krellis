#!/usr/bin/env bash
# Undo/redo of layout edits: a keyboard move, a native resize commit (one step), a cancelled resize (none),
# a closed window is not resurrected by redo, and minimizing is not undone. Seed: scenarios/undo.rc (toasts on, glide on).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
layout() { call dump | grep -E "^p[0-9]+ " | sed -E 's/ opacity.*//' | sort; }
hist() { call dump | sed -n 's/^HISTORY //p'; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
differs() { if [ "$2" != "$3" ]; then echo "PASS $1"; else echo "FAIL $1: unchanged"; fi; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1.5
check "history empty after opening windows" "$(hist)" "undo 0 redo 0"
s0=$(layout)
call moveDirection left; sleep 1.5
s1=$(layout); differs "move changed the layout" "$s0" "$s1"
check "move recorded" "$(hist)" "undo 1 redo 0"
call undo; sleep 1.5
check "undo restores the layout" "$(layout)" "$s0"
check "undo toast" "$(call dump | sed -n 's/^TOAST \(Undid: [a-z ]*\) age.*/\1/p')" "Undid: move tile"
check "undo moved the step to redo" "$(hist)" "undo 0 redo 1"
call redo; sleep 1.5
check "redo reapplies the move" "$(layout)" "$s1"
call undo; sleep 1.5
call undo; sleep 0.3
check "nothing to undo says so" "$(call dump | sed -n 's/^TOAST \(Nothing to undo\) age.*/\1/p')" "Nothing to undo"
echo "--- close a window between undo and redo"
panels_before=$(call dump | grep -cE "^p[0-9]+ ")
call closeActive; sleep 1.5
panels_after=$(call dump | grep -cE "^p[0-9]+ ")
check "a window closed" "$panels_after" "$((panels_before - 1))"
call redo; sleep 1.5
check "redo does not resurrect the closed window" "$(call dump | grep -cE "^p[0-9]+ ")" "$panels_after"
/home/ivan/dev/kwin-trellis/tools/shot undo-after-close >/dev/null
echo "--- native resize: one step; a cancelled resize records none"
before=$(hist); l0=$(layout)
call simNativeResizeStart 0 1.0 0.5 >/dev/null; call simNativeResizeMove 60 0 >/dev/null; sleep 0.4; call simNativeResizeEnd true >/dev/null; sleep 1.2
differs "resize changed the layout" "$l0" "$(layout)"
after=$(hist); echo "history $before -> $after"
call undo; sleep 1.5
check "one undo reverts the whole resize" "$(layout)" "$l0"
call redo; sleep 1.2
h=$(hist)
call simNativeResizeStart 0 1.0 0.5 >/dev/null; call simNativeResizeMove 60 0 >/dev/null; sleep 0.4; call simNativeResizeEnd false >/dev/null; sleep 1
check "cancelled resize records nothing" "$(hist)" "$h"
echo "--- refused while a drag is in progress"
call simDragStart 0 200 300 >/dev/null; call undo; sleep 0.3
check "undo during drag is refused" "$(call dump | sed -n 's/^TOAST \(Finish the drag first\) age.*/\1/p')" "Finish the drag first"
call simDragEnd false >/dev/null
echo "--- minimize is not undone"
call toggleHide 0; sleep 1.5
hid=$(call dump | grep -ciE "hidden")
call moveDirection right; sleep 1.2; call undo; sleep 1.5
check "undo keeps the window minimized" "$(call dump | grep -ciE "hidden")" "$hid"
pkill -P "$$" -x alacritty
