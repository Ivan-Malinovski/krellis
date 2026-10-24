#!/usr/bin/env bash
# Small helper windows (a fixed-size prompt, an undecorated always-on-top bar) keep their own size and place and take
# no tile; a small but ordinary resizable window still tiles.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
c++ -fPIC "$KRELLIS_ROOT/scenarios/small-client.cpp" -o "$NEST_WORK/small-client" $(pkg-config --cflags --libs Qt6Widgets)
for n in A B; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
check "two tiles" "$(order)" "p1 p2"
"$NEST_WORK/small-client" fixed prompt >/dev/null 2>&1 &
sleep 3
check "a fixed-size window takes no tile" "$(order)" "p1 p2"
"$NEST_WORK/small-client" bar sharebar >/dev/null 2>&1 &
sleep 3
check "an undecorated always-on-top bar takes no tile" "$(order)" "p1 p2"
check "the tiles keep their size" "$(call dump | grep "^p1 " | sed -n 's/.* screen [0-9.,]* \([0-9.]*x[0-9.]*\) .*/\1/p')" "628.0x788.0"
"$NEST_WORK/small-client" plain ordinary >/dev/null 2>&1 &
sleep 3
check "a small resizable window still tiles" "$(order | wc -w)" 3
pkill -P "$$" -x alacritty; pkill small-client
