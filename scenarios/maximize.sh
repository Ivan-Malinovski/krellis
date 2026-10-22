# A native maximize (title-bar double click) becomes Krellis's panel maximize and gives the window its tile back.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
echo "--- before"; call dump | grep -E "^camera|^p2"
cat >"$NEST_WORK/max.js" <<'JS'
for (const w of workspace.windowList()) if (w.caption === 'win-B') w.setMaximize(true, true);
JS
n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/max.js" max)
qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null
sleep 2
echo "--- after native maximize of B"; call dump | grep -E "^camera|^p"
rc=0
cam=$(call dump | grep -E "^camera")
case "$cam" in "camera 0 0 1 1"*) echo "FAIL camera did not zoom to B: $cam"; rc=1;; *) echo "OK camera moved to B: $cam";; esac
# The native state was given back, so a second double click toggles Krellis's maximize off again.
n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/max.js" max2)
qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null
sleep 2
cam=$(call dump | grep -E "^camera")
case "$cam" in "camera 0 0 1 1"*) echo "OK second maximize restored the overview: $cam";; *) echo "FAIL second maximize left $cam"; rc=1;; esac
call dump | grep -E "^p"
pkill -P "$$" -x alacritty
exit $rc
