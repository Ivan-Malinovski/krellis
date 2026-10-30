# Aurorae render-cache workaround

`aurorae-6.7.5-render-cache.patch` applies to the official KDE Aurorae v6.7.5 sources. The modified upstream file is GPL-2.0-or-later; the patch is provided under that same license, separately from Krellis's MIT code. Upstream source and notices remain in the isolated build directory. No Aurorae implementation is vendored into the Krellis effect.

The seven added calls disable persistent rendering-cache insertion for full-frame decoration and mask SVGs. Small button SVGs and current in-memory frame pixmaps are retained. `tools/build-aurorae-fix.sh` downloads a checksum-pinned source archive, applies the patch and builds/stages a uniquely named plugin. It does not install files or change the live compositor.

See [the investigation and activation notes](../docs/aurorae-cache-fix-2026-10-30.md).
