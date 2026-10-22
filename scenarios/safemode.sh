#!/usr/bin/env bash
# Crash-loop guard. SAFEMODE_EXPECT=load|refuse; NEST_BOOT_STRIKES seeds the marker the previous start left (unset: none).
#   unset -> loads, marker strikes 0     NEST_BOOT_STRIKES=0 -> loads (one early death), marker strikes 1
#   NEST_BOOT_STRIKES=1 -> refuses (second early death in a row), marker untouched
# Three invocations, one nested run each.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
marker="$NEST_WORK/state/krellis/boot.json"
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
sleep 2
loaded=no; qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.version >/dev/null 2>&1 && loaded=yes
echo "marker: $(cat "$marker" 2>/dev/null)"
case "${SAFEMODE_EXPECT:?}" in
  load)
    check "effect loaded" "$loaded" yes
    check "marker is a fresh unclean start" "$(grep -o '"cleanExit":false' "$marker")" '"cleanExit":false'
    want=0; [ -n "${NEST_BOOT_STRIKES:-}" ] && want=$((NEST_BOOT_STRIKES + 1))
    check "marker carries the strikes" "$(grep -o '"strikes":[0-9]*' "$marker")" "\"strikes\":$want";;
  refuse)
    check "effect refused to load" "$loaded" no
    check "marker left alone" "$(grep -o '"strikes":[0-9]*' "$marker")" '"strikes":1'
    check "refusal logged" "$(grep -c 'Krellis not loaded' "$NEST_OUT/kwin.log" | sed 's/^[1-9][0-9]*$/yes/')" yes;;
esac
