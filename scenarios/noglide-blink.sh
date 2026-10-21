# glide=false: the drawn rect is final from the first frame, so a client that has not landed its size yet must be
# drawn at its own (frame) size, never stretched to the final rect (the content "blink"). Run with NEST_TRELLISRC
# set to glide=false. Samples frame vs drawn vs screen width through a maximize and a Ctrl+scroll zoom.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
c++ -fPIC /home/ivan/dev/kwin-trellis/scenarios/slow-client.cpp -o "$NEST_WORK/slow" $(pkg-config --cflags --libs Qt6Widgets)
for n in A B C; do "$NEST_WORK/slow" "win-$n" >/dev/null 2>&1 & sleep 1.5; done
sleep 1
rm -f "$NEST_OUT/samples.txt"
sample() {
  for i in $(seq 1 30); do call dump | grep -E "^p[0-9]+ " | sed -E 's/ app [^ ]+ drawn / drawn /' | tee -a "$NEST_OUT/samples.txt"; done
}
call toggleMaximize 1; sample; sleep 1
call toggleMaximize 1; sample; sleep 1
call simAxis -120 640 400 67108864; sample; sleep 1
rc=0
# Every sample of every window: drawn is either exactly the client's own frame (held while a client lays out)
# or exactly the final rect. Never a stretched or half-moved mixture, which would show gaps or a blink.
awk 'function abs(v){return v<0?-v:v}
  { for (i=1;i<=NF;i++) { if ($i=="screen") { sp=$(i+1); split($(i+2),a,"x"); sw=a[1]; sh=a[2] } if ($i=="frame") { fp=$(i+1); split($(i+2),b,"x"); fw=b[1]; fh=b[2] } if ($i=="drawn") { split($(i+1),c,"x"); dw=c[1]; dh=c[2] } if ($i=="drawnpos") dp=$(i+1) }
    held = abs(dw-fw)<=2 && abs(dh-fh)<=2 && dp==fp
    final = abs(dw-sw)<=2 && abs(dh-sh)<=2 && dp==sp
    if (sw>0 && !held && !final) { print "FAIL mixture:", $0; bad=1 } } END { exit bad }' "$NEST_OUT/samples.txt" && echo "ok never stretched or half-moved" || rc=1
pkill -P "$$" -x slow
exit $rc
