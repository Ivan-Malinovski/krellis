# The emergency "Turn Off" action must unload the effect and hand windows back at their native size.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis "$@"; }
for n in A B; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
js="$NEST_WORK/geo.js"
cat >"$js" <<'JS'
console.warn('GEO ' + JSON.stringify(workspace.windowList().filter(w => w.caption.startsWith('win-')).map(w => [w.caption, Math.round(w.frameGeometry.width), Math.round(w.frameGeometry.height)])));
JS
geo() {
  id=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$js" "geo-$1")
  qdbus6 org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script.run >/dev/null
  sleep 0.5
  grep 'GEO ' "$NEST_OUT/kwin.log" | tail -1 | sed 's/.*GEO //'
}
echo "--- tiled"; call org.kde.kwin.KWin.KrellisEffect.dump | grep "^p"
echo "tiled geometry: $(geo tiled)"
echo "loaded before: $(qdbus6 org.kde.KWin /Effects org.freedesktop.DBus.Properties.Get org.kde.kwin.Effects loadedEffects | tr '\n' ' ')"
call org.kde.kwin.KWin.KrellisEffect.turnOff
sleep 1.5
if call org.kde.kwin.KWin.KrellisEffect.dump >/dev/null 2>&1; then echo "FAIL /Krellis still registered"; rc=1; else echo "OK /Krellis gone"; rc=0; fi
loaded=$(qdbus6 org.kde.KWin /Effects org.freedesktop.DBus.Properties.Get org.kde.kwin.Effects loadedEffects | tr '\n' ' ')
echo "loaded after: $loaded"
case "$loaded" in *krellis*) echo "FAIL still loaded"; rc=1;; esac
echo "native geometry: $(geo after)"
/home/ivan/dev/krellis/tools/shot after-turnoff
pkill -P "$$" -x alacritty
exit $rc
