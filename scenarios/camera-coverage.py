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
def requests():
    return int(re.search(r'NATIVE-REQUESTS (\d+)',call('dump'))[1])
def check(ok, label):
    print(('PASS ' if ok else 'FAIL ')+label, flush=True)
    assert ok, label

def sample(label, points):
    assert call('configure',json.dumps(points),probe=True)=='true'
    time.sleep(.14)
    data=json.loads(call('stats',probe=True))
    (out/(label+'.json')).write_text(json.dumps(data,indent=2))
    check(all(p['samples']>=3 and p['bad']==0 for p in data['points']), label)
    call('configure','[]',probe=True)

def geometry():
    row=next(line for line in call('dump').splitlines() if "'zoom-load-1' screen" in line)
    def rect(key):
        return tuple(map(float,re.search(key+r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',row).groups()))
    return row, rect

def axis_points(axis):
    # Decoration borders shift the client-space markers. Use the logical drawn
    # transform, not a hard-coded undecorated origin (nor a buffer-cover fit).
    row, rect = geometry()
    fx, fy, fw, fh = rect('frame')
    cx, cy, _, _ = rect('client')
    dw, dh = map(float,re.search(r' drawn ([\d.]+)x([\d.]+)',row).groups())
    dx, dy = map(float,re.search(r' drawnpos ([-\d.]+),([-\d.]+)',row).groups())
    if axis == 'y':
        y = dy + (cy-fy+105)*dh/fh
        return [dict(x=dx+dw*f,y=y,color='#ff00ff') for f in (.35,.75)]
    x = dx + (cx-fx+105)*dw/fw
    return [dict(x=x,y=dy+dh*f,color='#00ffff') for f in (.35,.75)]

log=(out/'client.txt').open('w')
client=subprocess.Popen([os.environ['NEST_WORK']+'/client','2','--axis-markers'],stdout=log,stderr=subprocess.DEVNULL)
pointer=subprocess.Popen([os.environ['NEST_WORK']+'/pointer-driver'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
assert pointer.stdout.readline().strip()=='READY'
def event(command):
    pointer.stdin.write(command+'\n'); pointer.stdin.flush()
    assert pointer.stdout.readline().strip()=='OK'
try:
    for _ in range(100):
        if len(re.findall(r'^p\d+ ',call('dump'),re.M))==2: break
        time.sleep(.05)
    else: raise AssertionError('two windows required')
    time.sleep(1)
    _, rect=geometry(); original=rect('frame')
    before=requests()
    call('focusIndex',1)
    time.sleep(.12)
    row, rect=geometry()
    check(0 < requests()-before <= 2,'horizontal zoom configures the visible destination immediately')
    check(abs(rect('frame')[3]-original[3]) <= 1,'horizontal zoom keeps native height unchanged')
    sample('horizontal-axis',axis_points('y'))
    check(requests()-before <= 2,'horizontal animation adds no geometry requests')
    time.sleep(.5)
    call('overview'); time.sleep(.7)
    call('simMiddleButton',640,400,'true'); call('simMiddleButton',640,400,'false')
    time.sleep(.7)
    _, rect=geometry(); original=rect('frame')
    check(original[2] > 1200 and original[3] < 500,'fixture now has two vertically stacked tiles')
    before=requests()
    call('focusIndex',1)
    time.sleep(.12)
    row, rect=geometry()
    check(0 < requests()-before <= 2,'vertical zoom configures the visible destination immediately')
    check(abs(rect('frame')[2]-original[2]) <= 1,'vertical zoom keeps native width unchanged')
    # A client-space cyan stripe at x=100 keeps its X position throughout.
    # Uniform cover would enlarge it horizontally and move it away.
    sample('vertical-axis',axis_points('x'))
    check(requests()-before <= 2,'vertical animation adds no geometry requests')
    time.sleep(.5)
    row, rect=geometry()
    fx,fy,fw,fh=rect('frame'); bx,by,_,_=rect('client')
    dw,dh=map(float,re.search(r' drawn ([\d.]+)x([\d.]+)',row).groups())
    dx,dy=map(float,re.search(r' drawnpos ([-\d.]+),([-\d.]+)',row).groups())
    x,y=dx+(bx+300-fx)*dw/fw,dy+(by+200-fy)*dh/fh
    event(f'move {x} {y}'); event('down'); event('up'); time.sleep(.1)
    delivered=(out/'client.txt').read_text()
    for kind in ('CLICK','RELEASE'):
        matches=re.findall(rf'^{kind} 1 ([\d.]+) ([\d.]+)$',delivered,re.M)
        check(bool(matches) and abs(float(matches[-1][0])-300)<1 and abs(float(matches[-1][1])-200)<1,
              kind.lower()+' maps correctly after vertical-only camera navigation')
    (out/'settled.txt').write_text(call('dump'))
finally:
    pointer.terminate(); pointer.wait(timeout=5)
    client.terminate(); client.wait(timeout=10)
    log.close()
