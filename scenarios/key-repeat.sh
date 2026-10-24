#!/usr/bin/env bash
# Exercises the registered QAction and pre-shortcut key observer through simulation.
# Does not establish physical global-shortcut delivery; no live compositor input.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
for n in A B C D E; do
    alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 &
    sleep 1
done
sleep 1
python3 - <<'PY'
import subprocess, time, re

def call(method,*args):
    return subprocess.check_output(['qdbus6','org.kde.KWin','/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.'+method,*map(str,args)],text=True)
def dump(): return call('dump')
def layout(): return '\n'.join(line.split(' frame ')[0] for line in dump().splitlines() if re.match(r'^p\d+ ',line))
def check(ok,label):
    print(('PASS ' if ok else 'FAIL ')+label,flush=True)
    if not ok: raise SystemExit(1)
def key(k,down): call('simKey',k,'true' if down else 'false')
def action(direction): call('simShortcut','KrellisMove'+direction)
META=16777250
F=70
A=65
call('focusDirection','left')
call('focusDirection','left')
call('focusDirection','left')
call('focusDirection','left')
time.sleep(.4)
initial=layout()
key(META,True); key(F,True); action('Right')
one=layout()
check(one!=initial,'initial step is immediate')
check(re.search(r'^KEY-MOVE p\d+ held 1$',dump(),re.M),'selected tile has a held outline')
# Duplicate QAction notifications from OS repeat cannot accelerate our timer.
action('Right'); action('Right'); time.sleep(.10)
check(layout()==one,'duplicate shortcut notifications do not move again during the pause')
time.sleep(.32)
two=layout()
check(two!=one,'held key repeats after the initial pause')
time.sleep(.18)
check(layout()!=two,'held key continues at a steady cadence')
key(F,False)
stopped=layout()
check('held 1' not in dump(),'key release drops the held outline')
time.sleep(.5)
check(layout()==stopped,'key release stops repetition')
check('KEY-MOVE  held 0' in dump(),'release outline finishes fading')
# A fast tap may be released before the asynchronous global action arrives.
key(A,True); key(A,False)
before=layout(); action('Left'); after=layout()
check(after!=before,'delayed action after a fast tap still moves once')
time.sleep(.5)
check(layout()==after,'released tap never starts a repeating hold')
# A second physical press works even when direction has not changed.
key(A,True); action('Left'); before=layout(); key(META,False)
time.sleep(.5)
check(layout()==before,'releasing Meta stops repetition while the letter remains down')
key(A,False)
key(META,True); key(F,True); action('Right')
call('focusDirection','left'); stopped=layout()
time.sleep(.5)
check(layout()==stopped,'changing focused window stops the held move')
key(F,False); key(META,False)
# An unrelated key cancels; its normal handling remains available.
key(META,True); key(F,True); action('Right'); key(88,True)
stopped=layout(); time.sleep(.5)
check(layout()==stopped,'another key cancels the held move')
key(88,False); key(F,False); key(META,False)
print(dump(),flush=True)
PY
pkill -P "$$" -x alacritty
