# Remaining Kitty navigation lag

Follow-up: Kitty backgrounds were subsequently made opaque at Ivan’s request. He reports some improvement but whole-desktop pauses remain. A more representative shared-instance workload exposed repeated overview-label QML scene recreation; see the final section of [zoom-performance.md](zoom-performance.md) for the measured fix. The investigation below records the earlier controls.

Ivan reports that Brave stays smooth while Kitty gets slower deeper in the layout. Disabling blur helps but does not remove it. He has not yet tested fully opaque Kitty in that same live workload. The preceding frame-allocation pass mainly benefits opt-in icon mode, so a noticeable live improvement was not established.

No production rendering, settings or live effect was changed in this investigation. Tests add independent Kitty opacity/blur-radius controls, optional blur-effect loading, configurable depth-test output size, per-frame timestamps and opt-in asynchronous OpenGL timestamp pairs. A new zoom-burst fixture adds rapid wheel sweeps. These diagnostics are nested-only.

## Comparisons

All runs are serial, use 25 task-owned Kitty processes with --config NONE and sleep, a 3440×1440 virtual output, the Hyprland profile and the paint probe. First compare opacity 1 with blur disabled, opacity .8 with blur disabled, and opacity .8/radius 16 with KWin blur loaded. Then repeat opacity .88/blur disabled with the live rounded-corner settings and peek=80, disable the rounded-corner effect only in the nested session, and repeat opaque Kitty. Installed upstream ShapeCorners is also loaded by default in the first matrix; it is not a clean Krellis-only rendering comparison.

The ordinary depth sweep does not reproduce progressively worse transparency-only performance. At inward step 6, opaque/transparent CPU processing p95 is 1.447/1.528 ms, GPU timeline p95 .964/.823 ms; at step 11 CPU p95 is .684/.835 ms. With matching corner settings, transparent step-6 CPU/GPU timeline p95 is 1.370/.786 ms; without the corner effect it is 1.167/.465 ms. Removing corners reduces some work but does not establish the reported depth trend or a Kitty-versus-Brave cause.

Blurred runs retain occasional larger spans, including step-11 CPU p95 2.113 ms and GPU timeline maximum 19.935 ms. That does not isolate the remaining blur-disabled live issue. Rapid sweeps (12 wheel events 60 ms apart, three inward/outward repeats) show opaque CPU p95 1.724–2.244 ms and transparent p95 1.820–2.447 ms; no clear growing separation emerges. These runs use empty sleeping terminals, not the user's actual shell workloads, real outputs or 144 Hz presentation.

Raw summaries: [kitty-transparency artifact](artifacts/kitty-transparency-2026-10-30.json). Six serial depth sweeps pass 109 assertions each. The final zoom-burst fixture passes seven checks; camera-coverage passes eleven real-pixel/input checks and output-damage passes three real-frame checks with the updated probe. Release probe build and diff/syntax checks pass. No full suite, commit or live installation.

## Measurement limits

CPU spans include driver waits. GPU fields are asynchronous GL timestamp pairs around paintScreen, collected only after GL_QUERY_RESULT_AVAILABLE; they do not wait for query completion. They measure GPU timeline elapsed time, which can include command-stream waits and CPU submission gaps, not isolated shader execution or end-to-end presentation latency. Trailing unavailable queries are omitted; idle/no-op phases can have no GPU samples. drawStats counts callbacks including recursive offscreen redirects, not unique windows or true fragment overdraw. No native-shadow-only draws appeared in the first matrix, so that particular initial hypothesis gained no support.

ShapeCorners' published [drawWindow implementation](https://github.com/matinlotfali/KDE-Rounded-Corners/blob/v0.10.0/src/Effect.cpp) redirects affected windows through KWin's offscreen path; the installed package is a local build, so the source inspection alone cannot establish its exact live cost. Local measurements above are the evidence for the small nested difference.

## Reproduce

```
# Build the test-only probe explicitly.
cmake --build build --target krellis_paint_probe
ZOOM_DEPTH_SIZE="3440 1440" ZOOM_LOAD_BLUR=0 ZOOM_LOAD_OPACITY=0.88 ZOOM_LOAD_BLUR_RADIUS=0 ZOOM_DEPTH_BLUR=0 KRELLIS_PROBE_GPU_TIMING=1 tools/scenarios.sh -j 1 -k zoom-depth
ZOOM_DEPTH_SIZE="3440 1440" ZOOM_LOAD_OPACITY=0.88 ZOOM_LOAD_BLUR_RADIUS=0 KRELLIS_PROBE_GPU_TIMING=1 tools/scenarios.sh -j 1 -k zoom-burst
```

Set opacity to 1 for the opaque control. Default zoom-depth still explicitly loads real blur. Its blur-role assertions verify Krellis policy, not that blur is enabled globally in the opacity controls. Keep experimental effects out of the live compositor. The useful next distinction is fully opaque versus transparent Kitty in the same live tree, followed by a workload that reproduces any remaining gap before proposing another visual tradeoff.
