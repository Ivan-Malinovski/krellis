#!/usr/bin/env bash
# Drag windows with real fake-input pointer events while the paint probe watches presented frames for black flashes.
set -euo pipefail
[[ "$NEST_SOCKET" == krellis-test-* ]]
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT
cat >"$XDG_CONFIG_HOME/krellisrc" <<'RULES'
[ViewRules]
decor-alpha\minWidth=900
decor-alpha\minHeight=1000
decor-beta\minWidth=900
decor-beta\minHeight=1000
decor-gamma\minWidth=900
decor-gamma\minHeight=1000
RULES
qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.reloadRules
c++ -std=c++20 -fPIC "$project/scenarios/pointer-driver.cpp" -o "$NEST_WORK/pointer-driver" $(pkg-config --cflags --libs KWaylandClient wayland-client)
c++ -fPIC "$project/scenarios/damage-client.cpp" -o "$NEST_WORK/damage-client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
for specification in 'alpha #d23c41' 'beta #35bd69' 'gamma #3699de'; do
    read -r name color <<<"$specification"
    "$NEST_WORK/damage-client" "decor-$name" "$color" >"$NEST_OUT/client-$name.txt" 2>&1 & clients="$clients $!"
    sleep 0.3
done
sleep 1.2
python3 - "$NEST_WORK" "$NEST_OUT" <<'PY'
import json,pathlib,re,subprocess,time,sys
work,out=map(pathlib.Path,sys.argv[1:])
def call(method,*args):
    return subprocess.check_output(['qdbus6','org.kde.KWin','/Krellis','org.kde.kwin.KWin.KrellisEffect.'+method,*map(str,args)],text=True).strip()
def probe(method,*args):
    return subprocess.check_output(['qdbus6','org.kde.KWin','/KrellisPaintProbe','org.kde.kwin.KrellisPaintProbe.'+method,*map(str,args)],text=True).strip()
def title(name):
    line=next(l for l in call('dump').splitlines() if "'decor-"+name+"' screen " in l)
    r=lambda k: tuple(map(float,re.search(k+r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',line).groups()))
    s,f,c=r('screen'),r('frame'),r('client')
    return s[0]+s[2]*.5, s[1]+(c[1]-f[1])*.5*s[3]/f[3]
pointer=subprocess.Popen([str(work/'pointer-driver')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
assert pointer.stdout.readline().strip()=='READY'
def event(s):
    pointer.stdin.write(s+'\n'); pointer.stdin.flush()
    assert pointer.stdout.readline().strip()=='OK',s
results={}
try:
    call('overview');time.sleep(.8)
    assert probe('watchBlack','true')=='true'
    time.sleep(1.0)
    results['idle']=json.loads(probe('blackStats'))
    for phase,names in [('drag-tiled',['gamma','beta','alpha']),('drag-float',None)]:
        if names is None:
            call('toggleDock',2);time.sleep(.7); names=['gamma']
        probe('watchBlack','true')
        for n in names:
            x,y=title(n)
            event(f'move {x} {y}');time.sleep(.06);event('down');time.sleep(.2)
            for i in range(40):  # wander across the canvas, ~60 fps
                event(f'move {x+i*14%420-100} {y+(i*9)%200-60}');time.sleep(.016)
            time.sleep(.3)
            subprocess.call([str(pathlib.Path(__import__('os').environ['KRELLIS_ROOT']+'/tools/shot')),'mid-'+phase+'-'+n])
            event('up');time.sleep(.8)
        results[phase]=json.loads(probe('blackStats'))
    (out/'black.json').write_text(json.dumps(results,indent=1))
    print(json.dumps(results))
    assert results['idle']['frames']>10 and results['idle']['blackFrames']==0,results  # drag phases are diagnostic: the lifted slot is an unpainted hole
    print('PASS idle shows no black frames; drag counts above are informational')
finally:
    if pointer.poll() is None:
        pointer.stdin.write('quit\n');pointer.stdin.flush();pointer.wait(timeout=3)
PY
