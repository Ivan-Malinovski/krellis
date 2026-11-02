#!/usr/bin/env bash
# Install the effect plugin into the system Qt plugin dir (needs sudo). It does NOT enable the effect.
# The plugin is ABI-locked to the KWin it was built against; reinstall after every KWin upgrade.
# Every install gets a fresh plugin id and file name (krellis_<stamp>): KWin keeps an already-mapped plugin file
# for the life of the process, so reusing the name would keep running the old code until a logout.
set -euo pipefail
project=$(cd -- "$(dirname -- "$0")/.." && pwd)
# Every run configures and builds against the KWin that is installed, so there is no expected version to check here:
# the effect refuses to load into a KWin other than the one it was built for, logs why and sends one notification.
export PATH=$HOME/.local/bin:$PATH
id=krellis_$(date +%m%d%H%M%S)
cmake -S "$project" -B "$project/build-live" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="$project/third_party/ecm-prefix" \
  -DVulkan_INCLUDE_DIR="$project/third_party/vulkan-headers/include" -DKRELLIS_BUILD_EFFECT=ON -DKRELLIS_BUILD_KCM=ON -DKRELLIS_ID="$id" >/dev/null
touch "$project/effect/multicanvas.cpp"   # refresh the build stamp
cmake --build "$project/build-live" --target krellis kcm_krellis
sudo "$(command -v cmake)" --install "$project/build-live" --prefix /usr
# Unlinking a plugin KWin has mapped is safe; the running copy keeps its inode until it is swapped out.
plugins=/usr/lib/qt6/plugins/kwin/effects/plugins
for old in "$plugins"/krellis*.so "$plugins"/trellis*.so; do [ -e "$old" ] || continue; [ "$(basename "$old" .so)" = "$id" ] || sudo rm -f "$old"; done
echo
echo "Installed $id. Try it (this session only; a logout or tools/live.sh off removes it):"
echo '  tools/live.sh on     # swaps any loaded Krellis for this build; also: Meta+Alt+Escape turns it off'
echo '  tools/live.sh status # running and installed build stamps should match'
