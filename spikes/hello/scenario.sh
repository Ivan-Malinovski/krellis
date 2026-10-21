export WAYLAND_DISPLAY=$NEST_SOCKET
kwrite >/dev/null 2>&1 &
sleep 4
/home/ivan/dev/kwin-trellis/tools/shot hello
