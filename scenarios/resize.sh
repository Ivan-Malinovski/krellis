#!/usr/bin/env bash
# Exercise real native KWin resize signals and inspect model/paint/persistence.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT
project=$KRELLIS_ROOT
split_delta=${KRELLIS_RESIZE_DELTA:-65}
zoom_delta=35
if [ "$split_delta" -lt 0 ]; then zoom_delta=-35; fi
c++ -fPIC "$project/scenarios/lifecycle-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
for app in alpha beta gamma; do
    extra=
    if [ "$app" = alpha ]; then extra=--large-min; fi
    "$NEST_WORK/client" "resize-$app" "resize-$app" $extra >"$NEST_OUT/client-$app.txt" 2>&1 & clients="$clients $!"
    sleep 0.3
done
sleep 1
call overview; sleep 0.8
save() { call saveSession | grep -x true; cp "$XDG_STATE_HOME/krellis/session.json" "$NEST_OUT/$1.json"; call dump >"$NEST_OUT/$1.txt"; }
save baseline
call simNativeResizeStart 0 1 0.5 | grep -x true
call dump | tee "$NEST_OUT/start.txt" | grep 'RESIZE .*floating 0 boundaries 1'
call simNativeResizeMove "$split_delta" 0 | grep -x true
sleep 0.2
call dump | tee "$NEST_OUT/preview.txt" | grep -E "RESIZE .*nativeSteps [1-9]"
call saveSession | grep -x false
call simNativeResizeEnd false | grep -x true
sleep 0.8
save cancelled
call simNativeResizeStart 0 1 0.5 | grep -x true
call simNativeResizeMove "$split_delta" 0 | grep -x true
sleep 0.2
call simNativeResizeEnd true | grep -x true
sleep 0.8
save committed
call simPinch 0.8 640 350
call simPinchEnd; sleep 1
save zoomed
call simNativeResizeStart 0 1 0.5 | grep -x true
call simNativeResizeMove "$zoom_delta" 0 | grep -x true
sleep 0.2
call dump >"$NEST_OUT/zoom-preview.txt"
call simNativeResizeEnd false | grep -x true
sleep 0.8
# Use a large native minimum to force a genuine paint scale for the float.
cat >"$XDG_CONFIG_HOME/krellisrc" <<'RULES'
[ViewRules]
resize-alpha\minWidth=1200
resize-alpha\minHeight=1000
RULES
call reloadRules
call toggleDock 0
sleep 0.8
save floated
call simNativeResizeStart 0 1 1 | grep -x true
call simNativeResizeMove 60 45 | grep -x true
sleep 0.2
call dump >"$NEST_OUT/float-preview.txt"
call simNativeResizeEnd false | grep -x true
sleep 0.8
save float-cancelled
call simNativeResizeStart 0 1 1 | grep -x true
call simNativeResizeMove 60 45 | grep -x true
sleep 0.2
call simNativeResizeEnd true | grep -x true
sleep 0.8
save float-committed
call simNativeResizeStart 0 0 0 | grep -x true
call simNativeResizeMove -40 -30 | grep -x true
sleep 0.2
call dump >"$NEST_OUT/opposite-preview.txt"
call simNativeResizeEnd false | grep -x true
sleep 0.8
# Dock/undock must retain the accepted normalized floating size.
call toggleDock 0; sleep 0.6
call toggleDock 0; sleep 0.6
save float-recalled
call simNativeResizeStart 0 0 0 | grep -x true
call simNativeResizeMove -30 -20 | grep -x true
sleep 0.2
call reloadRules
sleep 0.6
save rule-cancelled
! grep -q '^RESIZE ' "$NEST_OUT/rule-cancelled.txt"
python3 - "$NEST_OUT" "$split_delta" "$zoom_delta" <<'PY'
import json,pathlib,re,sys
root=pathlib.Path(sys.argv[1])
def doc(name): return json.loads((root/(name+'.json')).read_text())['canvases'][0]
def row(name): return next(l for l in (root/(name+'.txt')).read_text().splitlines() if "'resize-alpha'" in l)
def rect(name,key='screen'):
    return tuple(map(float,re.search(key+r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',row(name)).groups()))
def floats(name): return doc(name)['floating']
assert doc('baseline')['root']==doc('cancelled')['root']
assert doc('baseline')['root']!=doc('committed')['root']
a=rect('baseline'); b=rect('preview')
assert abs((b[0]+b[2])-(a[0]+a[2])-float(sys.argv[2]))<2,(a,b)
a=rect('zoomed'); b=rect('zoom-preview')
assert abs((b[0]+b[2])-(a[0]+a[2])-float(sys.argv[3]))<2,(a,b)
assert floats('floated')==floats('float-cancelled')
a=rect('floated'); b=rect('float-preview'); native=rect('floated','frame')
assert a[2]/native[2] < .9 or a[3]/native[3] < .9,(a,native)
assert abs(b[2]-a[2]-60)<2 and abs(b[3]-a[3]-45)<2,(a,b)
assert floats('floated')!=floats('float-committed')
assert floats('float-committed')[0]['rect']==floats('float-recalled')[0]['rect']
assert floats('rule-cancelled')==floats('float-recalled')
a=rect('float-committed'); b=rect('opposite-preview')
assert abs(b[0]-a[0]+40)<2 and abs(b[1]-a[1]+30)<2,(a,b)
assert abs((b[0]+b[2])-(a[0]+a[2]))<2 and abs((b[1]+b[3])-(a[1]+a[3]))<2,(a,b)
print('PASS native split resize, cancel, commit, scaled corner delta, float persistence and remembered size')
PY
