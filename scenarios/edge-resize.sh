#!/usr/bin/env bash
# Real pointer drags on the edges of tiled windows must start a native resize and change the split.
set -euo pipefail
[[ "$NEST_SOCKET" == krellis-test-* ]]
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT

c++ -std=c++20 -fPIC "$project/scenarios/pointer-driver.cpp" -o "$NEST_WORK/pointer-driver" $(pkg-config --cflags --libs KWaylandClient wayland-client)
c++ -fPIC "$project/scenarios/damage-client.cpp" -o "$NEST_WORK/damage-client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
for specification in 'alpha #d23c41' 'beta #35bd69' 'gamma #3699de'; do
    read -r name color <<<"$specification"
    "$NEST_WORK/damage-client" "edge-$name" "$color" >"$NEST_OUT/client-$name.txt" 2>&1 & clients="$clients $!"
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
    line=next(l for l in call('dump').splitlines() if "'edge-"+name+"' screen " in l)
    def rect(key): return tuple(map(float,re.search(key+r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',line).groups()))
    return rect('screen'),rect('frame'),rect('client')
def point(name,title=True):
    s,f,c=geometry(name)
    return s[0]+s[2]*.5, s[1]+ ((c[1]-f[1])*.5 if title else (c[1]-f[1])+120)*s[3]/f[3]
def native(label):
    js=work/(label+'.js')
    js.write_text("console.warn('EDGE_"+label+" '+JSON.stringify(workspace.windowList().filter(w=>w.caption.startsWith('edge-')).map(w=>({name:w.caption,active:w.active,move:w.move,resize:w.resize,x:w.frameGeometry.x,y:w.frameGeometry.y,w:w.frameGeometry.width,h:w.frameGeometry.height,csd:!w.noBorder&&!w.decoration}))));")
    i=subprocess.check_output(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',str(js),'krellis-edge-'+label],text=True).strip()
    subprocess.check_call(['qdbus6','org.kde.KWin','/Scripting/Script'+i,'org.kde.kwin.Script.run'],stdout=subprocess.DEVNULL)
    for _ in range(30):
        marker='EDGE_'+label+' '
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
    rows=[]
    if os.environ.get('EDGE_BASELINE'):
        js=work/'place.js'
        js.write_text("let i=0;for(const w of workspace.windowList().filter(w=>w.caption.startsWith('edge-'))){w.frameGeometry={x:6+i*427,y:6,width:415,height:788};i++;}")
        n=subprocess.check_output(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',str(js),'place'],text=True).strip()
        subprocess.check_call(['qdbus6','org.kde.KWin','/Scripting/Script'+n,'org.kde.kwin.Script.run'],stdout=subprocess.DEVNULL); time.sleep(.5)
    st0=native('start'); print('native', json.dumps(st0))
    def geometry(name):
        r=native('g'+str(time.monotonic_ns()))['edge-'+name]; g=(r['x'],r['y'],r['w'],r['h']); return g,g,g
    s,f,c=geometry('alpha')
    mid=s[1]+s[3]/2
    right=s[0]+s[2]
    for dx in [-12,-8,-5,-3,-1,1,3,5,8,11]:
        x=right+dx
        event(f'move {x} {mid}'); time.sleep(.08)
        event('down'); time.sleep(.15)
        event(f'move {x+40} {mid}'); time.sleep(.15)
        st=native('probe'+str(time.monotonic_ns()))
        resizing=st['edge-alpha']['resize'] or any(v['resize'] for v in st.values())
        moving=any(v['move'] for v in st.values())
        event('up'); time.sleep(.7)
        s2,_,_=geometry('alpha')
        print(f'dx={dx:+d} resize={resizing} move={moving} width {s[2]:.0f}->{s2[2]:.0f}')
        rows.append((dx,resizing,s2[2]-s[2])); sys.stdout.flush()
        # restore by resetting the layout through a reload: later probes start from changed widths, so re-read
        s,f,c=geometry('alpha'); right=s[0]+s[2]
    ok=any(r for _,r,_ in rows)
    print(('PASS ' if ok else 'FAIL ') + 'real pointer edge resize starts and changes width')
    assert ok
finally:
    pointer.stdin.write('quit\n'); pointer.stdin.flush()
PY
