#!/usr/bin/env bash
# Remove the installed plugin. Turn the effect off first so KWin is not holding the file.
set -euo pipefail
"$(dirname -- "$0")/live.sh" off || true
sudo rm -f /usr/lib/qt6/plugins/kwin/effects/plugins/trellis*.so \
  /usr/lib/qt6/plugins/kwin/effects/configs/kcm_trellis.so /usr/lib/qt6/plugins/plasma/kcms/systemsettings/kcm_trellis.so \
  /usr/share/applications/kcm_trellis.desktop
echo Removed.
