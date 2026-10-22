#!/usr/bin/env bash
# The plugin refuses to load into a KWin other than the one it was built against.
#   MISMATCH_EXPECT=load     KWin as it is: loads
#   MISMATCH_EXPECT=refuse   KRELLIS_ASSUME_KWIN=6.7.0 stands in for another running KWin: refuses and says why
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
sleep 2
loaded=no; qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.version >/dev/null 2>&1 && loaded=yes
note="$NEST_WORK/state/krellis/kwin-mismatch"
case "${MISMATCH_EXPECT:?}" in
  load)
    check "effect loaded" "$loaded" yes
    check "no mismatch note" "$([ -e "$note" ] && echo yes || echo no)" no;;
  refuse)
    check "effect refused to load" "$loaded" no
    check "refusal logged" "$(grep -c 'Krellis not loaded: it was built for KWin' "$NEST_OUT/kwin.log" | sed 's/^[1-9][0-9]*$/yes/')" yes
    check "refusal remembered once" "$(cat "$note" 2>/dev/null | grep -c -- '->6.7.0')" 1;;
esac
