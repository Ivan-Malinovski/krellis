#!/usr/bin/env bash
# Real Wayland fake-input events, not /Krellis simButton: exercise native routing too.
set -euo pipefail
[[ "$NEST_SOCKET" == krellis-test-* ]]
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=/home/ivan/dev/krellis
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
import json,os,pathlib,re,subprocess,time,sys
work,out=map(pathlib.Path,sys.argv[1:])
def call(method,*args):
    return subprocess.check_output(['qdbus6','org.kde.KWin','/Krellis','org.kde.kwin.KWin.KrellisEffect.'+method,*map(str,args)],text=True).strip()
def dump(label):
    s=call('dump'); (out/(label+'.txt')).write_text(s); return s
def geometry(name):
    line=next(l for l in call('dump').splitlines() if "'decor-"+name+"' screen " in l)
    def rect(key): return tuple(map(float,re.search(key+r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',line).groups()))
    return rect('screen'),rect('frame'),rect('client')
def point(name,title=True):
    s,f,c=geometry(name)
    return s[0]+s[2]*.5, s[1]+ ((c[1]-f[1])*.5 if title else (c[1]-f[1])+120)*s[3]/f[3]
def native(label):
    js=work/(label+'.js')
    js.write_text("console.warn('DECOR_"+label+" '+JSON.stringify(workspace.windowList().filter(w=>w.caption.startsWith('decor-')).map(w=>({name:w.caption,active:w.active,move:w.move,resize:w.resize}))));")
    i=subprocess.check_output(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',str(js),'krellis-decor-'+label],text=True).strip()
    subprocess.check_call(['qdbus6','org.kde.KWin','/Scripting/Script'+i,'org.kde.kwin.Script.run'],stdout=subprocess.DEVNULL)
    for _ in range(30):
        marker='DECOR_'+label+' '
        for line in (out/'kwin.log').read_text().splitlines():
            if marker in line:
                rows=json.loads(line.split(marker,1)[1]); (out/(label+'.json')).write_text(json.dumps(rows)); return {r['name']:r for r in rows}
        time.sleep(.03)
    raise AssertionError('No native snapshot '+label)
pointer=subprocess.Popen([str(work/'pointer-driver')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
assert pointer.stdout.readline().strip()=='READY'
def event(s):
    pointer.stdin.write(s+'\n'); pointer.stdin.flush()
    assert pointer.stdout.readline().strip()=='OK',s
try:
    call('overview');time.sleep(.8)
    def probe(method,*args):
        return subprocess.check_output(['qdbus6','org.kde.KWin','/KrellisPaintProbe','org.kde.kwin.KrellisPaintProbe.'+method,*map(str,args)],text=True).strip()
    def idle_damage(label):
        points=[]
        for name,color in [('alpha','#d23c41'),('beta','#35bd69'),('gamma','#3699de')]:
            s,f,c=geometry(name)
            x,y=point(name,False)
            points.append(dict(x=x,y=y,color=color))
            points.append(dict(x=s[0]+(c[0]-f[0]+19)*s[2]/f[2],y=s[1]+(c[1]-f[1]+19)*s[3]/f[3],blink=True))
        (out/(label+'-points.json')).write_text(json.dumps(points))
        dump(label+'-geometry')
        assert probe('configure',json.dumps(points))=='true'
        time.sleep(1.3)
        stats=json.loads(probe('stats'));(out/(label+'.json')).write_text(json.dumps(stats))
        assert stats['frames']>=6,stats
        if any(p['bad'] for p in stats['points']):
            subprocess.check_call(['/home/ivan/dev/krellis/tools/shot',label+'-failed'])
        assert all(p['bad']==0 for p in stats['points']),stats
        assert all(p['seen']==3 for p in stats['points'][1::2]),stats
        assert probe('configure','[]')=='false'
    idle_damage('idle-damage')
    call('simPinch',.8,640,350);call('simPinchEnd');time.sleep(1)
    idle_damage('zoomed-idle-damage')
    call('overview');time.sleep(1)
    # A translated/scaled right-hand title has global x > native frame width.
    for n in ['gamma','beta','alpha','gamma']:
        x,y=point(n)
        event(f'move {x} {y}'); time.sleep(.06); event('down'); time.sleep(.25)
        event(f'move {x+36} {y+24}'); time.sleep(.12)
        state=native('drag-'+n+str(time.monotonic_ns()))
        assert state['decor-'+n]['move'] and not state['decor-'+n]['resize'],state
        assert not any(v['move'] or v['resize'] for k,v in state.items() if k!='decor-'+n),state
        event('up');time.sleep(.7)
        state=native('release-'+str(time.monotonic_ns()))
        assert not any(v['move'] or v['resize'] for v in state.values()),state
    # A real body click must reach the visible client, despite overlapping rest frames.
    for n in ['alpha','gamma','beta']:
        counts={a:(out/('client-'+a+'.txt')).read_text().count('CLICK ') for a in ['alpha','beta','gamma']}
        x,y=point(n,False);event(f'move {x} {y}');event('down');event('up');time.sleep(.1)
        for a,before in counts.items():
            after=(out/('client-'+a+'.txt')).read_text().count('CLICK ')
            assert after-before==(1 if a==n else 0),(n,a,before,after)
    # The float is painted above native frames at unrelated positions.
    call('toggleDock',2);time.sleep(.7)
    assert ' FLOAT' in next(l for l in call('dump').splitlines() if "'decor-gamma'" in l)
    x,y=point('gamma');event(f'move {x} {y}');event('down');time.sleep(.25)
    event(f'move {x+28} {y+16}');time.sleep(.1)
    state=native('floating-title')
    assert state['decor-gamma']['move'] and not state['decor-gamma']['resize'],state
    event('up');time.sleep(.7)
    if ' FLOAT' not in next(l for l in call('dump').splitlines() if "'decor-gamma'" in l):
        call('toggleDock',2);time.sleep(.7)
    s,_,_=geometry('gamma')
    x,y=s[0]+.25,s[1]+.25
    event(f'move {x} {y}');event('down');time.sleep(.25)
    event(f'move {x-24} {y-16}');time.sleep(.12)
    state=native('floating-corner')
    assert state['decor-gamma']['resize'] and not state['decor-gamma']['move'],state
    event('up');time.sleep(.7)
    state=native('corner-release')
    assert not any(v['move'] or v['resize'] for v in state.values()),state
    dump('final')
    # Keep blinking partial surface damage alive while capturing several settled frames.
    for i in range(3):
        time.sleep(.17)
        subprocess.check_call([str(pathlib.Path('/home/ivan/dev/krellis/tools/shot')), 'idle-'+str(i)])
    print('PASS real title-bar move, scaled translated targets, consecutive grabs, releases, visible-client delivery floating title and true corner resize')
finally:
    if pointer.poll() is None:
        pointer.stdin.write('quit\n');pointer.stdin.flush();pointer.wait(timeout=3)
PY
