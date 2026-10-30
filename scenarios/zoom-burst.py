"""Rapid camera input with real clients; retain CPU/GPU timeline samples without FPS assertions."""
import os,subprocess,time,json,re
from pathlib import Path
out=Path(os.environ['NEST_OUT'])
width,height=map(int,os.environ.get('ZOOM_DEPTH_SIZE','1280 800').split())
def call(method,*args,probe=False):
 path,iface=('/KrellisPaintProbe','org.kde.kwin.KrellisPaintProbe') if probe else ('/Krellis','org.kde.kwin.KWin.KrellisEffect')
 return subprocess.check_output(['qdbus6','org.kde.KWin',path,iface+'.'+method,*map(str,args)],text=True).strip()
count=int(os.environ.get('ZOOM_BURST_COUNT','25'))
repeats=int(os.environ.get('ZOOM_BURST_REPEATS','3'))
p=subprocess.Popen([os.environ['NEST_WORK']+'/client',str(count)])
try:
 for _ in range(240):
  if len(re.findall(r'^p\d+ ',call('dump'),re.M))==count:break
  time.sleep(.1)
 else:raise AssertionError(f'{count} clients required')
 time.sleep(1)
 call('overview');time.sleep(.7)
 if os.environ.get('ZOOM_BURST_SHOTS')=='1':
  subprocess.run(['spectacle','-b','-f','-n','-o',str(out/'overview-initial.png')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 for repeat in range(repeats):
  for direction in (-15,15):
   before=call('dump');call('timing','true',probe=True)
   for step in range(12):
    call('simAxis',direction,width-80,height-100,0x10000000)
    time.sleep(.06)
   time.sleep(.8)
   call('timing','false',probe=True)
   data=json.loads(call('timingJson',probe=True))
   data['before']=before;data['after']=call('dump')
   builds_before=re.search(r'^MOTION .* builds (\d+) shown ([01])',before,re.M)
   builds_after=re.search(r'^MOTION .* builds (\d+) shown ([01])',data['after'],re.M)
   if builds_before and builds_after:
    assert builds_before[1]==builds_after[1], 'navigation rebuilt the overview QML scene'
    if builds_after[2]=='0':
     assert re.search(r'^MOTION .* labels 0 ',data['after'],re.M), 'hidden overlay reports stale labels'
   if os.environ.get('ZOOM_BURST_ASSERT_REUSE')=='1':
    assert builds_before and builds_after, 'overlay reuse diagnostics required'
   assert len(re.findall(r'^p\d+ ',data['after'],re.M))==count, 'window ownership lost'
   assert len(data['cpuMs'])>=3, 'rendered frame samples required'
   assert data['before'].splitlines()[0]!=data['after'].splitlines()[0], 'camera destination must change'
   print(f'PASS rapid sweep {repeat} direction {direction} renders and preserves ownership',flush=True)
   (out/f'burst-{repeat}-{direction}.json').write_text(json.dumps(data))
   if repeat==0 and os.environ.get('ZOOM_BURST_SHOTS')=='1':
    subprocess.run(['spectacle','-b','-f','-n','-o',str(out/f'settled-{direction}.png')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 print('PASS rapid inward/outward wheel sweeps complete',flush=True)
finally:
 p.terminate();p.wait(timeout=10)
