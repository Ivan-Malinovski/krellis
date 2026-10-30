"""Measure verified deep-to-overview transitions, including synchronous input handling."""
import json,os,re,subprocess,time
import dbus
from pathlib import Path
out=Path(os.environ['NEST_OUT'])
width,height=map(int,os.environ.get('ZOOM_DEPTH_SIZE','1280 800').split())
count=int(os.environ.get('ZOOM_DEEP_COUNT','14'))
bus=dbus.SessionBus()
interfaces={False:dbus.Interface(bus.get_object('org.kde.KWin','/Krellis'),'org.kde.kwin.KWin.KrellisEffect'),
            True:dbus.Interface(bus.get_object('org.kde.KWin','/KrellisPaintProbe'),'org.kde.kwin.KrellisPaintProbe')}
def call(method,*args,probe=False):
    args=[True if arg=='true' else False if arg=='false' else arg for arg in args]
    return str(interfaces[probe].get_dbus_method(method)(*args,timeout=10))
def check(ok,label):
    print(('PASS ' if ok else 'FAIL ')+label,flush=True)
    assert ok,label
def camera(state):return tuple(map(float,re.match(r'camera ([\d.e+-]+) ([\d.e+-]+) ([\d.e+-]+) ([\d.e+-]+)',state).groups()))
p=subprocess.Popen([os.environ['NEST_WORK']+'/client',str(count)])
try:
    for _ in range(300):
        if len(re.findall(r'^p\d+ ',call('dump'),re.M))==count:break
        time.sleep(.1)
    else:raise AssertionError('required clients missing')
    time.sleep(1)
    if os.environ.get('ZOOM_DEEP_SWAP_LIBRARY'):
        replacement=os.environ['ZOOM_DEEP_SWAP_LIBRARY']
        subprocess.run(['kwriteconfig6','--file',str(Path(os.environ['XDG_CONFIG_HOME'],'kwinrc')),
            '--group','org.kde.kdecoration2','--key','library',replacement],check=True)
        kwin=dbus.Interface(bus.get_object('org.kde.KWin','/KWin'),'org.kde.KWin')
        kwin.reconfigure()
        for _ in range(100):
            if replacement in str(kwin.supportInformation()):break
            time.sleep(.05)
        else:raise AssertionError('decoration swap failed')
        time.sleep(.7)
        check(len(re.findall(r'^p\d+ ',call('dump'),re.M))==count,'hot decoration swap retains windows')
    if os.environ.get('AURORAE_TEST_ID'):
        support=str(dbus.Interface(bus.get_object('org.kde.KWin','/KWin'),'org.kde.KWin').supportInformation())
        check(os.environ['AURORAE_TEST_ID'] in support,'requested decoration plugin is active')
    call('overview');time.sleep(.7)
    if os.environ.get('ZOOM_DEEP_SHOTS')=='1':
        subprocess.run(['spectacle','-b','-f','-n','-o',str(out/'overview.png')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    (out/'overview.txt').write_text(call('dump'))
    for cycle in range(int(os.environ.get('ZOOM_DEEP_REPEATS','3'))):
        call('focusIndex',count-1);time.sleep(.8)
        start=call('dump');c=camera(start)
        if cycle==0 and os.environ.get('ZOOM_DEEP_SHOTS')=='1':
            subprocess.run(['spectacle','-b','-f','-n','-o',str(out/'deep.png')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        check(min(c[2:])<.02,f'starts deeply zoomed (cycle {cycle}, camera {c})')
        call('timing','true',probe=True)
        events=[];destinations={c}
        for step in range(count+2):
            begin=time.monotonic()
            call('simAxis',15,width-80,height-100,0x10000000)
            elapsed=(time.monotonic()-begin)*1000
            state_begin=time.monotonic()
            state=call('dump');state_ms=(time.monotonic()-state_begin)*1000
            dest=camera(state);destinations.add(dest)
            events.append(dict(step=step,inputMs=elapsed,stateMs=state_ms,camera=dest))
            time.sleep(.22) # > Navigator's 180ms quiet threshold: actual distinct steps
            if re.search(r"^framed ''$",state,re.M):break
        heartbeat=[]
        deadline=time.monotonic()+1.0
        while time.monotonic()<deadline:
            begin=time.monotonic();call('version')
            heartbeat.append((time.monotonic()-begin)*1000)
            time.sleep(.05)
        call('timing','false',probe=True)
        data=json.loads(call('timingJson',probe=True));data['events']=events;data['heartbeatMs']=heartbeat
        data['before']=start;data['after']=call('dump')
        save_start=time.monotonic();saved=call('saveSession')
        data['explicitSaveMs']=(time.monotonic()-save_start)*1000
        check(saved=='1','explicit save completes successfully')
        (out/f'deep-out-{cycle}.json').write_text(json.dumps(data))
        check(len(destinations)>=6,f'traverses distinct outward destinations ({len(destinations)})')
        check(re.search(r"^framed ''$",data['after'],re.M),'reaches full overview')
        check(len(re.findall(r'^p\d+ ',data['after'],re.M))==count,'retains window ownership')
        if cycle==0 and os.environ.get('ZOOM_DEEP_SHOTS')=='1':
            subprocess.run(['spectacle','-b','-f','-n','-o',str(out/'returned.png')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        print('MEASURE',cycle,'cpuMax',max(data['cpuMs'],default=0),'inputMax',max(e['inputMs'] for e in events),flush=True)
finally:
    p.terminate();p.wait(timeout=10)
