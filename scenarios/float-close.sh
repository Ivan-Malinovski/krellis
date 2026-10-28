#!/usr/bin/env bash
# Closing a floating window under the Hyprland profile, next to KWin's own Scale effect (run with NEST_EFFECTS
# krellis,scale and a fast AnimationDurationFactor): the effect claims the close, the closing copy shrinks and fades
# from where the float was, and nothing else moves.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
rc=0
check() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1: $2"; rc=1; fi; }
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & [ $n = B ] && pidB=$!; sleep 1.5; done
sleep 1.5
call simForceFloat 1
call simDragStart 1 640 400
for x in 600 560 520 500; do call simDragMove $x 300; sleep 0.05; done
call simDragEnd true; sleep 1.5
call simForceFloat -1
before=$(call dump | grep -E "^p[13] ")
call dump | grep -q "^p2 .*FLOAT" && check float-made ok || check float-made "win-B is not floating"
kill "$pidB"
seen=0; bad=0; first=""; last=""
for i in $(seq 1 60); do
    d=$(call dump)
    c=$(grep "^CLOSING win-B" <<<"$d")
    if [ -n "$c" ]; then
        seen=$((seen + 1))
        [ -z "$first" ] && first=$c
        last=$c
    fi
    [ "$(grep -E "^p[13] " <<<"$d")" = "$before" ] || bad=$((bad + 1))
    sleep 0.012
done
[[ "$first" == *" grabbed 1" ]] && check close-claimed ok || check close-claimed "the closing copy does not hold the close grab: $first"
[ "$seen" -ge 3 ] && check closing-animates ok || check closing-animates "only $seen samples of a closing copy"
[ "$bad" -eq 0 ] && check neighbours-still ok || check neighbours-still "$bad samples where a tile changed"
# the closing copy ends smaller and fainter than it began
w0=$(sed -E 's/.* drawn [-0-9.]+,[-0-9.]+ ([0-9.]+)x.*/\1/' <<<"$first"); w1=$(sed -E 's/.* drawn [-0-9.]+,[-0-9.]+ ([0-9.]+)x.*/\1/' <<<"$last")
awk -v a="$w0" -v b="$w1" 'BEGIN { exit !(b < a) }' && check closing-shrinks ok || check closing-shrinks "width $w0 -> $w1"
call dump | grep -q "^CLOSING win-B" && check closing-released "still held" || check closing-released ok
pkill -P "$$" -x alacritty
exit $rc
