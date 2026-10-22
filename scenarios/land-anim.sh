# glide=false with the default landMs: when a client's real geometry changes, the window plays a short move-and-scale
# from the old rect to the new one (content already laid out at the final size). Samples one window through a maximize.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
rm -f "$NEST_OUT/samples.txt"
call toggleMaximize 1
for i in $(seq 1 60); do call dump | grep -E "^p2 " | sed -E 's/ app [^ ]+ drawn / drawn /' >>"$NEST_OUT/samples.txt"; done
sleep 1
call dump | grep -E "^p2 " | sed -E 's/ app [^ ]+ drawn / drawn /' >>"$NEST_OUT/samples.txt"
rc=0
awk 'function abs(v){return v<0?-v:v}
  { for (i=1;i<=NF;i++) { if ($i=="screen") { split($(i+2),a,"x"); sw=a[1] } if ($i=="frame") { split($(i+2),b,"x"); fw=b[1] } if ($i=="drawn") { split($(i+1),c,"x"); dw=c[1] } }
    if (dw>0 && abs(dw-fw)>2 && abs(dw-sw)>2) mid++
    last=$0; lastd=dw; lastf=fw; lasts=sw }
  END { print "intermediate samples:", mid; if (mid<2 || abs(lastd-lasts)>2 || abs(lastf-lasts)>2) { print "FAIL no animation or not settled:", last; exit 1 } }' "$NEST_OUT/samples.txt" && echo "ok landing animation plays and settles" || rc=1
pkill -P "$$" -x alacritty
exit $rc
