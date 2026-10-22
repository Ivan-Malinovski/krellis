# The overlays follow the Plasma colour scheme and accent, switch live, and fall back to the built-in palette.
# Run: NEST_KRELLISRC=scenarios/theme.rc NEST_KDEGLOBALS=<light scheme + AccentColor> tools/nested.sh ... (see theme.rc)
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
call toggleBar >/dev/null
sleep 1.5
echo "--- light scheme, custom accent"; call dumpTheme
/home/ivan/dev/krellis/tools/shot theme-light >/dev/null
echo "--- switch to the dark scheme"
{ cat /usr/share/color-schemes/BreezeDark.colors; printf '\n[General]\nAccentColor=255,106,0\n'; } >"$NEST_WORK/config/kdeglobals"
call reloadSettings; sleep 1; call dumpTheme
/home/ivan/dev/krellis/tools/shot theme-dark >/dev/null
echo "--- followTheme off"
printf '[General]\nglide=true\nmap=true\nmapPinned=true\nfollowTheme=false\n' >"$NEST_WORK/config/krellisrc"
call reloadSettings; sleep 1; call dumpTheme
/home/ivan/dev/krellis/tools/shot theme-literal >/dev/null
pkill -P "$$" -x alacritty
