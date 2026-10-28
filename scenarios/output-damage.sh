#!/usr/bin/env bash
# NEST_OUTPUTS=2. Probe real frames after idle damage, animation, and transfer.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL output damage fixture exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
# Build krellis_paint_probe explicitly before running; the .env loads it at
# compositor startup so its task-owned socket check sees the outer display.
cat >>"$XDG_CONFIG_HOME/krellisrc" <<'RULES'

[ViewRules]
damage-red\minWidth=900
damage-red\minHeight=1000
damage-green\minWidth=900
damage-green\minHeight=1000
damage-blue\minWidth=900
damage-blue\minHeight=1000
RULES
qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.reloadRules
c++ -fPIC "$KRELLIS_ROOT/scenarios/damage-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
python3 - "$NEST_WORK/client" "$NEST_OUT" <<'PY'
import json, pathlib, re, subprocess, sys, time
client, out = sys.argv[1], pathlib.Path(sys.argv[2])
clients=[]
def call(method,*args):
    return subprocess.check_output(['qdbus6','org.kde.KWin','/Krellis','org.kde.kwin.KWin.KrellisEffect.'+method,*map(str,args)],text=True).strip()
def probe(method,*args):
    return subprocess.check_output(['qdbus6','org.kde.KWin','/KrellisPaintProbe','org.kde.kwin.KrellisPaintProbe.'+method,*map(str,args)],text=True).strip()
colors={'red':'#d23c41','green':'#35bd69','blue':'#3699de'}
def verify(label):
    points=[]
    for output in (0,1):
        text=call('dumpOutput',output)
        for line in text.splitlines():
            name=next((n for n in colors if "'damage-"+n+"' screen " in line),None)
            if not name: continue
            r=lambda k: tuple(map(float,re.search(k+r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',line).groups()))
            s,f,c=r('screen'),r('frame'),r('client')
            assert s[2]/f[2]<.95 or s[3]/f[3]<.95, line
            points.append(dict(x=s[0]+s[2]*.75,y=s[1]+s[3]*.6,color=colors[name]))
            points.append(dict(x=s[0]+(c[0]-f[0]+19)*s[2]/f[2],y=s[1]+(c[1]-f[1]+19)*s[3]/f[3],blink=True))
    assert len(points)==6,points
    assert probe('configure',json.dumps(points))=='true'
    time.sleep(1.4)
    stats=json.loads(probe('stats'))
    (out/(label+'.json')).write_text(json.dumps(stats,indent=2))
    assert all(p['samples']>=6 and p['bad']==0 for p in stats['points']),stats
    assert all(p['seen']==3 for p in stats['points'][1::2]),stats
    assert probe('configure','[]')=='false'
    print('PASS '+label+' real frames preserve scaled surface damage on both outputs',flush=True)
try:
    for name,color in colors.items():
        clients.append(subprocess.Popen([client,'damage-'+name,color],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
        time.sleep(.3)
    call('selectOutput',0);call('moveToOutput',2,1)
    time.sleep(1)
    verify('idle')
    call('selectOutput',0);call('toggleMaximize',0)
    time.sleep(.6)
    call('toggleMaximize',0)
    time.sleep(.6)
    verify('after-animation')
    call('moveToOutput',1,1)
    time.sleep(.7)
    verify('after-transfer')
finally:
    for p in clients:
        if p.poll() is None:p.terminate()
    for p in clients:p.wait(timeout=5)
PY
