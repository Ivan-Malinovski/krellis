export WAYLAND_DISPLAY=$NEST_SOCKET
kwrite >/dev/null 2>&1 &
sleep 4
$KRELLIS_ROOT/tools/shot hello
