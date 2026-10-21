#!/usr/bin/env bash
# The settings module opens and shows trellisrc, and reloadSettings applies an edited file to the running effect.
# Seed NEST_TRELLISRC with a few rules to see them in the module.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
rc=0
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
call toggleMaximize 1; sleep 1.5
before=$(call dump | grep -E "^p2 " | sed -E 's/.* screen ([-0-9.]+),.*/\1/'); before=${before%.*}
echo "--- gutter before: framed tile at $before"
echo "--- open the module"
kcmshell6 kcm_trellis >"$NEST_OUT/kcm.txt" 2>&1 &
kcm=$!
sleep 6
/home/ivan/dev/kwin-trellis/tools/shot settings-kcm >/dev/null
kill -0 $kcm 2>/dev/null && echo "OK module still running" || { echo "FAIL module exited"; rc=1; }
echo "--- peek=0 and a rule, then reloadSettings"
printf '[General]\nglide=true\npeek=0\nfloatFirst=true\n\n[ViewRules]\nAlacritty\\closable=false\n' >"$XDG_CONFIG_HOME/trellisrc"
call reloadSettings; sleep 1.5
after=$(call dump | grep -E "^p2 " | sed -E 's/.* screen ([-0-9.]+),.*/\1/'); after=${after%.*}
echo "framed tile now at $after"
if [ "$after" -lt "$before" ]; then echo "OK the gutter closed: $before -> $after"; else echo "FAIL gutter unchanged: $before -> $after"; rc=1; fi
call dump | grep -q NO_CLOSE && echo "OK rule reloaded" || { echo "FAIL rule not reloaded"; rc=1; }
kill $kcm 2>/dev/null
pkill -P "$$" -x alacritty
exit $rc
