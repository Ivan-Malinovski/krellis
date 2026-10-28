#!/usr/bin/env bash
# Closing a floating window under the Hyprland profile: sample the presented frames where it stood. After the window
# has faded out, its own colour must not come back (a flash of the whole closed window).
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
probe() { qdbus6 org.kde.KWin /KrellisPaintProbe org.kde.kwin.KrellisPaintProbe."$@"; }
c++ -fPIC "$KRELLIS_ROOT/scenarios/damage-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
pids=()
i=0
for color in '#d23c41' '#35bd69' '#3699de'; do
    i=$((i + 1))
    "$NEST_WORK/client" "close-$i" "$color" >/dev/null 2>&1 &
    pids+=($!)
    sleep 0.6
done
sleep 1
call simForceFloat 1
call simDragStart 1 640 400
for x in 600 560 520 500; do call simDragMove $x 300; sleep 0.05; done
call simDragEnd true; sleep 1.5
call simForceFloat -1
row=$(call dump | grep "^p2 .*FLOAT")
[ -n "$row" ] && echo "PASS float-made" || { echo "FAIL float-made"; exit 1; }
export CLOSE_PID=${pids[1]}
python3 - "$row" "$NEST_OUT" <<'PY'
import json, pathlib, re, subprocess, sys, time, os, signal
row, out = sys.argv[1], pathlib.Path(sys.argv[2])
def probe(method,*args):
    return subprocess.check_output(['qdbus6','org.kde.KWin','/KrellisPaintProbe','org.kde.kwin.KrellisPaintProbe.'+method,*map(str,args)],text=True).strip()
s=tuple(map(float,re.search(r'screen ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',row).groups()))
pts=[dict(x=s[0]+s[2]*fx,y=s[1]+s[3]*fy,color='#000000') for fx in (.2,.5,.8) for fy in (.2,.5,.8)]
assert probe('configure',json.dumps(pts))=='true'
assert probe('history','true')=='true'
time.sleep(.3)
pid=int(os.environ["CLOSE_PID"])
t0=time.time(); os.kill(pid, signal.SIGTERM)
time.sleep(1.2)
hist=json.loads(probe('historyJson'))
(out/'history.json').write_text(json.dumps(hist))
green=(0x35,0xbd,0x69)
def near(px,c): return all(abs(px[i+1]-c[i])<14 for i in range(3))
bad=0
for n,rows in enumerate(hist):
    seq=[near(r,green) for r in rows]
    # the window's colour is present, then gone for a while, then back
    first=seq.index(True) if True in seq else -1
    state=0; frames_back=0
    for k,v in enumerate(seq):
        if state==0 and not v and k>0 and seq[k-1]: state=1
        elif state==1 and v: state=2; frames_back+=1
        elif state==2 and v: frames_back+=1
    print(f'point {n}: frames {len(seq)} window colour returned after leaving: {frames_back}')
    bad+=frames_back
print('PASS no-flash' if bad==0 else f'FAIL flash: {bad} frame samples show the closed window again')
PY
kill "${pids[@]}" 2>/dev/null
exit 0
