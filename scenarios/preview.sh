#!/usr/bin/env bash
set -euo pipefail
[[ "${NEST_SOCKET:-}" = trellis-nest || "${NEST_SOCKET:-}" == trellis-test-* ]]
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
clients=
trap 'for process in $clients; do kill "$process" 2>/dev/null || true; done' EXIT
cat >"$NEST_WORK/preview-bashrc" <<'RC'
PS1='trellis \w $ '
t() {
    local action=${1:-help} method
    case "$action" in
        overview) method=overview;;
        focus) method=focusIndex;;
        float) method=toggleDock;;
        hide) method=toggleHide;;
        resize)
            qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect.simNativeResizeStart "${2:-0}" 1 1 >/dev/null || return
            qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect.simNativeResizeMove "${3:-80}" "${4:-40}" >/dev/null
            qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect.simNativeResizeEnd true
            return;;
        move) method=simNativeResizeMove;;
        accept) qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect.simNativeResizeEnd true; return;;
        cancel) qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect.simNativeResizeEnd false; return;;
        *) printf '%s\n' 't overview      See all panels' 't focus 0       Focus panel 0 (opening order)' 't float 2       Dock/undock panel 2' 't hide 1        Hide/restore panel 1' 't resize 0 80 40 Resize panel 0 by 80x40'; return;;
    esac
    shift
    qdbus6 org.kde.KWin /Trellis "org.kde.kwin.KWin.TrellisEffect.$method" "$@"
}
printf '\n%s\n' 'Trellis preview — isolated desktop' 'Use Overview in the bottom bar, then Ctrl+Alt+drag to pan.' 'Hold Ctrl+Alt+V and drag to zoom; Ctrl+scroll steps the hierarchy.' 'Type t for panel controls. Exit the terminals to finish.' ''
t help
RC
for name in 'Panel 0' 'Panel 1' 'Panel 2 — floating'; do
    alacritty --option window.opacity=1.0 --title "$name" -e bash --noprofile --rcfile "$NEST_WORK/preview-bashrc" >"$NEST_OUT/client-${#clients}.log" 2>&1 &
    clients="$clients $!"
    sleep 0.4
done
sleep 0.8
qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect.toggleDock 2 >/dev/null
if [ "${TRELLIS_PREVIEW_SMOKE:-0}" = 1 ]; then
    sleep 0.8
    if [ "${TRELLIS_PREVIEW_TYPE_TEST:-0}" = 1 ]; then
        project=$(cd "$(dirname "$0")/.." && pwd)
        c++ -std=c++20 -fPIC "$project/scenarios/pointer-driver.cpp" -o "$NEST_WORK/pointer-driver" $(pkg-config --cflags --libs KWaylandClient wayland-client)
        python3 - "$NEST_WORK/pointer-driver" <<'PYTEST'
import subprocess,sys,time
p=subprocess.Popen([sys.argv[1]],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
assert p.stdout.readline().strip()=='READY'
for command in ['move 700 500','down','up']+['key 30']*100+['key 14']*100:
    p.stdin.write(command+'\n');p.stdin.flush()
    assert p.stdout.readline().strip()=='OK',command
    time.sleep(.01)
p.stdin.write('quit\n');p.stdin.flush();p.wait(timeout=3)
time.sleep(.7)
print('PASS typed and erased 100 characters through real keyboard input')
PYTEST
    fi
    "$(dirname "$0")/../tools/shot" preview
else
    for process in $clients; do wait "$process" || true; done
fi
