import json
import os
from pathlib import Path
import re
import subprocess
import time

out = Path(os.environ['NEST_OUT'])
def call(method, *args, probe=False):
    path, interface = ('/KrellisPaintProbe', 'org.kde.kwin.KrellisPaintProbe') if probe else ('/Krellis', 'org.kde.kwin.KWin.KrellisEffect')
    return subprocess.check_output(['qdbus6','org.kde.KWin',path,interface+'.'+method,*map(str,args)],text=True).strip()
def check(ok, label):
    print(('PASS ' if ok else 'FAIL ')+label, flush=True)
    return ok
def row(title):
    line=next(l for l in call('dump').splitlines() if f"'{title}' screen" in l)
    return tuple(map(float,re.search(r'screen ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',line).groups()))

def watch(label, action, title):
    """Samples one pixel row across the screen on every presented frame while `action` runs."""
    x0,y0,w0,h0=row(title)
    y=y0+h0*.3
    xs=list(range(0,1280,3))
    assert call('configure',json.dumps([dict(x=x,y=y,color='#000000') for x in xs]),probe=True)=='true'
    assert call('history','true',probe=True)=='true'
    time.sleep(.15)
    action()
    fades=set()
    end=time.time()+1.0
    while time.time()<end:
        fades.update(re.findall(r'^FADE \S+ (\w+)',call('dump'),re.M))
        time.sleep(.01)
    hist=json.loads(call('historyJson',probe=True))
    call('configure','[]',probe=True)
    (out/(label+'.json')).write_text(json.dumps(hist))
    frames=min(len(r) for r in hist)
    # Yellow is (255,255,0); the tile backgrounds are dull (value 100), so min(r,g)-b stays under 0.16 there.
    def amount(px): return max(0.0,min(1.0,(min(px[1],px[2])-px[3])/255))
    # Follow the left window's stripe (the second window can enter on the right during zoom-out). A hard swap
    # is a frame whose leftmost stripe is at full strength more than 120 px from every stripe of the previous
    # frame: the content moved inside the window in one frame. A crossfade instead shows the old stripe fading
    # out while the new one fades in. A gap is a frame without the stripe between frames that have it.
    def runs(a):
        out=[]
        for i,v in enumerate(a):
            if v<=.2: continue
            if out and i-out[-1][-1][0]<=3: out[-1].append((i,v))
            else: out.append([(i,v)])
        return [(sum(i for i,_ in r)/len(r)*3, max(v for _,v in r)) for r in out]
    partial=0; swaps=0; seen=[]; before=[]
    for f in range(frames):
        now=runs([amount(hist[i][f]) for i in range(len(xs))])
        if any(.2<peak<.7 for _,peak in now): partial+=1
        if now and before and now[0][1]>.85 and all(abs(now[0][0]-x)>120 for x,_ in before): swaps+=1
        seen.append(now[0][0] if now else None)
        before=now or before
    present=[i for i,c in enumerate(seen) if c is not None]
    gaps=sum(1 for i in range(present[0],present[-1]) if seen[i] is None) if present else frames
    print(f'{label}: frames {frames} blended {partial} hard-swaps {swaps} gaps {gaps} fade states {sorted(fades)}')
    print(label+' track '+' '.join('-' if c is None else str(round(c)) for c in seen[:40]))
    return gaps, swaps, fades

client=subprocess.Popen([os.environ['NEST_WORK']+'/client','2','--right-marker'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
ok=True
try:
    for _ in range(100):
        if len(re.findall(r'^p\d+ ',call('dump'),re.M))==2: break
        time.sleep(.05)
    else: raise AssertionError('two windows required')
    time.sleep(1)
    for n,(label,action) in enumerate((('zoom-in',lambda: call('focusIndex',0)),('zoom-out',lambda: call('overview')))):
        gaps,swaps,fades=watch(label,action,'zoom-load-0')
        if os.environ.get('KRELLIS_NO_CROSSFADE')!='1':
            ok&=check('blending' in fades, label+': the old layout is snapshotted and crossfaded')
        ok&=check(gaps==0, label+': the window never vanishes mid-transition')
        ok&=check(swaps==0, label+': no single-frame jump of the right-anchored stripe')
        time.sleep(.6)
    ok&=check(not re.search(r'^FADE ',call('dump'),re.M), 'snapshots are released at rest')
    (out/'settled.txt').write_text(call('dump'))
finally:
    client.terminate(); client.wait(timeout=10)
