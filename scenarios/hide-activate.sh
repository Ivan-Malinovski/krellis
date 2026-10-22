#!/usr/bin/env bash
# A hidden (minimized) window brought back by something other than Krellis (the start menu, a launcher, another app
# asking for focus) must return to its tile at the right size, exactly as a click on its dock chip does.
# Variants: a plain window; one the app had maximized natively; one that was fullscreen when it was hidden.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'pkill -P "$$" -x alacritty || true' EXIT
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
seq=0
run() { local n; seq=$((seq+1)); n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/$1.js" "$1-$seq"); qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null; }
js() { cat >"$NEST_WORK/$1.js" <<JS
for (const w of workspace.windowList()) if (w.caption === 'win-B') { $2 }
JS
}
js activate "workspace.activeWindow = w;"
js maximize "w.setMaximize(true, true);"
js fullscreen "w.fullScreen = true;"
js unfullscreen "w.fullScreen = false;"
show() { call dump | grep -E "^(ACTIVE|p2)" | sed 's/ frame/ FRAME/'; }
ESC=16777216; RET=16777220
for variant in plain framed palette; do
  echo "=== $variant"
  case $variant in framed) call toggleMaximize 0; sleep 1.5;; esac
  echo "--- before hide"; show
  call toggleHide 1; sleep 1.5
  echo "--- hidden"; show
  if [ $variant = palette ]; then
    call togglePalette; sleep 0.3; call simText "win-B"; sleep 0.3
    call simKey $RET true; call simKey $RET false
  else
    run activate
  fi
  sleep 0.2
  echo "--- 200ms after"; show
  sleep 1.8
  echo "--- settled"; show
  line=$(call dump | grep "^p2 ")
  [ "$(call dump | sed -n 's/^ACTIVE //p')" = p2 ] && echo "PASS $variant: focused" || echo "FAIL $variant: not focused"
  # drawn at its tile's size, and the client has landed at it (no stretch)
  w=$(sed -nE 's/^p2 .* screen [-0-9.]+,[-0-9.]+ ([0-9.]+)x[0-9.]+ .*/\1/p' <<<"$line"); dw=$(sed -nE 's/.* drawn ([0-9.]+)x.*/\1/p' <<<"$line"); fw=$(sed -nE 's/.* frame [-0-9.]+,[-0-9.]+ ([0-9.]+)x.*/\1/p' <<<"$line")
  awk -v a="$w" -v b="$dw" -v c="$fw" 'BEGIN{exit !((a-b < 1 && b-a < 1) && (a-c < 2 && c-a < 2))}' && echo "PASS $variant: drawn at the tile size, client landed" || echo "FAIL $variant: tile $w drawn $dw frame $fw"
  $KRELLIS_ROOT/tools/shot hide-activate-$variant >/dev/null
done
