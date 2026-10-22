# The bar is off by default (full-height canvas) and toggles on and off; saving a view works without it.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
rc=0
check() { if echo "$1" | grep -q "$2"; then echo "OK $3"; else echo "FAIL $3 ($2)"; rc=1; fi; }
d=$(call dump)
check "$d" '^BAR 0' "bar off by default"
check "$d" '^area [0-9.]*,[0-9.]* 1280x800' "canvas uses the full height"
call toggleBar >/dev/null; sleep 1
d=$(call dump)
check "$d" '^BAR 1' "bar toggled on"
check "$d" '^area [0-9.]*,[0-9.]* 1280x752' "canvas leaves 48px for the bar"
call toggleBar >/dev/null; sleep 1
check "$(call dump)" '^area [0-9.]*,[0-9.]* 1280x800' "full height again"
call saveView >/dev/null; sleep 0.5
d=$(call dump)
check "$d" '^NAMING 1' "save view starts naming without the bar"
check "$d" '^area [0-9.]*,[0-9.]* 1280x800' "naming does not reserve space"
call toggleMap >/dev/null; sleep 0.5
check "$(call dump)" '^MAP geometry .* shown 1' "map toggles without the bar"
pkill -P "$$" -x alacritty
exit $rc
