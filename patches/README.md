# Aurorae render-cache workaround

`aurorae-6.7.5-render-cache.patch` applies to the official KDE Aurorae v6.7.5 sources. The modified upstream file is GPL-2.0-or-later; the patch is provided under that same license, separately from Krellis's MIT code. Upstream source and notices remain in the isolated build directory. No Aurorae implementation is vendored into the Krellis effect.

The seven added calls disable persistent rendering-cache insertion for full-frame decoration and mask SVGs. Small button SVGs and current in-memory frame pixmaps are retained. `tools/build-aurorae-fix.sh` downloads a checksum-pinned source archive, applies the patch and builds/stages a uniquely named plugin. It does not install files or change the live compositor.

See [the investigation and activation notes](../docs/aurorae-cache-fix-2026-10-30.md).

# KWin SpringMotion header

`kwin-6.8-install-springmotion-header.patch` adds `effect/springmotion.h` to the effect headers KWin installs for development (`src/CMakeLists.txt`, Devel component). It applies unchanged to KWin v6.8.2 and to current master, which both ship `src/effect/springmotion.{h,cpp}` and export `KWin::SpringMotion` from libkwin while leaving the header out of the install list.

Nothing is vendored from KWin: the patch only adds a line to an install list, so it carries no upstream implementation. Upstream file licensing is unchanged (GPL-2.0-or-later), and this repo's MIT code is unaffected.

Until upstream installs the header, the effect declares the ABI it links against in `effect/kwin_springmotion.h` (an opaque, size-checked body). See [the upstream note](../docs/upstream-springmotion-header-2026-11-02.md) for how to drop that file once the header ships.
