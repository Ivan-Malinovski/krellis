# Zoom performance handoff

**Rollback update, 2026-10-30:** the subsequent blink/resize experiments were rejected and fully reverted to source `e308e79`. The restored build is `krellis_1030150556`, stamp `build Oct 30 2026 15:05:58`; Ivan confirms it is smooth again, with the original blink. Read the [window zoom blink handoff](window-zoom-blink-handoff-2026-10-30.md) for current status, recordings, failed approaches and test limitations. The live-state and commit/push requests below are historical, not current authorization.

Updated 2026-10-30 after installing the overview-scene and Aurorae cache fixes. The sections below retain the investigation history; later entries supersede earlier status notes.

## Current live state

- Installed and loaded effect ID: `krellis_1030115620`.
- Running and installed stamps match: `build Oct 30 2026 11:56:25` (`tools/live.sh status`).
- Installation used the dev installer, a temporary `sudo` to `pkexec` wrapper, then `tools/live.sh on` and `status`.
- Selected Aurorae plugin: `org.kde.kwin.aurorae.krellis_1030123148`; the FieldworkDark theme is unchanged. Kitty is opaque with blur disabled, as requested.
- Ivan reports improved performance and has requested committing and pushing all current project changes. Final precommit validation is in progress.

## What changed across the investigation

The first optimization pass reduced repeated window lookups, shared per-frame visual geometry, skipped offscreen/unreadable overlay work, and made minimap updates change-driven. The following Hyprland-profile pass culled transformed windows outside their render viewport and changed camera navigation to request destination sizes only for visible content tiles. A discrete zoom requests those sizes immediately; continuous gestures wait until release. Offscreen frames remain available, and vertical-only camera navigation retains its per-axis scaling. A uniform crop/cover experiment was rejected after Ivan reported that vertical zoom distorted and cropped window contents; do not bring it back.

Ivan then reported that zoom slowed down as he went deeper, including with Kitty. He confirmed the tested Kitty windows are transparent and blurred. The prior Kitty benchmark used `--config NONE` and was opaque; the nested harness also disables KWin blur by default. That workload had therefore missed the expensive path.

The reproducer now launches 25 real Kitty clients with opacity 0.8 and blur 16 in an isolated, virtual nested KWin, explicitly loads the real KWin blur effect, and steps repeatedly inward/outward with simulated Meta+wheel. KWin 6.7's blur effect sizes its background textures from the entire transformed blur region before clipping to the output; changing dimensions trigger new targets. Deep transforms can also leave tiny blur regions whose downsample levels become zero-sized. The baseline run logged 317 failed blur framebuffer creations across the 1280×800 sweep.

The current fix chooses `WindowForceBlurRole` in `KrellisCanvas::paintWindow()`, where KWin's final paint scale and current render viewport are known. It withholds forced blur if either painted frame side is below 32 logical pixels or above 125% of that viewport's corresponding side. The Hyprland profile also suspends blur during camera presentation flights and held camera gestures, including inherited popup painting; normal tiles regain blur when they settle. Window opacity and contents remain intact. The visible tradeoff is a sharper backdrop during camera motion and no backdrop blur on tiny/oversized tiles. Geometry, native resize timing, input mapping, the global KWin blur setting, and non-camera animation behavior are unchanged.

Relevant code is in `effect/krellis_effect.cpp` (`applyRest`, `prePaintWindow`, `paintWindow`, `dump`) and `effect/krellis_effect.h` (`Flight::cameraNavigation`). The current diagnostic dump shows `blur 0|1` on window rows. `scenarios/zoom-depth.py` covers the real Kitty/blur path; `scenarios/zoom-load-app.bash` accepts `ZOOM_LOAD_BLUR=1` for opt-in blurred Kitty benchmarks. The normal `zoom-load` fixture stays opaque for comparison with earlier results.

## Results and limits

For the same 25-client inward sequence at 1280×800, step 5 (zero-based) improved from 26.689 ms processing p95 and 33.726 ms frame-interval p95 to 1.803 ms and 17.480 ms. At 3440×1440, step 3 improved from 19.408/33.529 ms to 1.971/17.532 ms, and step 5 from 17.287/33.039 ms to 1.747/17.130 ms. The measurements compare the installed early-visible-sizing build with the new bounded/suspended blur build.

Processing time covers pre-paint through post-paint and includes render submission and driver waits. It does not isolate GPU execution. Samples include settled frames; later blur restoration or client catch-up can still produce a slower single frame (the 3440×1440 updated step-4 maximum was 17.752 ms). The runs use virtual outputs and do not establish the feel on the physical 144 Hz display, or performance with a busy application. Treat this as a measured improvement, not a claim that all jank is solved.

The full run summaries, metadata, and timing caveats are in `docs/artifacts/camera-blur-depth-2026-10-30.json`; the investigation and earlier optimization history are in `docs/zoom-performance.md`. Raw work directories are `/tmp/krellis-depth-wheel-{before,motion}` and `/tmp/krellis-depth-wide-{before,after}` for this session only.

## Live feedback

After installing this build, Ivan tried many Brave windows, which do not use backdrop blur, and reported that deep zooming felt smooth. He also tested Kitty and said it feels much better, though a tree full of Brave windows is still smoother. This real-session comparison supports the finding that KWin's blur work caused much of the depth-related slowdown with transparent Kitty. Kitty retains some extra unevenness under the same navigation workload; its source is not isolated yet. These are subjective observations, while the virtual nested timings above remain the quantitative comparison.

## Verification already completed

- Release effect build through `tools/install.sh` succeeded; the resulting plugin loaded in the live KWin session and `tools/live.sh status` confirmed matching stamps.
- `ctest --test-dir build --output-on-failure`: all 20 tests passed.
- `tools/scenarios.sh -j 2 -k camera-coverage camera-resize output-damage hyprland cross-aspect tabs pin close-probe float-close resize interrupt animation-cost-map`: all 12 scenarios passed.
- `tools/scenarios.sh -j 1 -k zoom-depth`: 109 assertions passed. It checks blur suspension/restoration, inward/outward destinations, held/released gestures, ownership, and absence of invalid blur GPU allocations.
- Logs: `/tmp/krellis-depth-ctest.log`, `/tmp/krellis-depth-regressions.log`, `/tmp/krellis-depth-scenario-final.log`.
- `git diff --check` passed. The worktree still contains earlier unrelated user changes; review `git status --short` before editing.

## Suggested next session

1. Ivan reports deep zoom feels smooth with many Brave windows and much better with Kitty, though Brave remains smoother. If narrowing the remaining gap, compare similar Kitty and Brave window counts and content at the same depths; separate motion frames from the first frame after blur returns. Use `zoom-depth` and inspect per-phase samples before changing animation timing or resize behavior.
2. A possible next experiment is a viewport-clipped blur implementation that preserves blur during motion. KWin's current effect allocates before output clipping, so this requires a careful paint-chain design or a KWin-side change; do not assume a clipped device region alone bounds the allocation. Pair any attempt with real-frame pixel checks for blurred transparency, two-output rendering, and framebuffer/allocation checks.
3. Re-run the focused `zoom-depth` and camera/input regressions after changes. Only broaden to the full scenario suite if preparing a commit; do not commit or push unless Ivan asks.
4. Never test experimental changes by loading them into the live session without Ivan's explicit go-ahead. When another live build is ready, follow the standing procedure in `AGENTS.md`: install via the dev installer with the `pkexec` wrapper, load with `tools/live.sh on`, then verify with `tools/live.sh status`.



## Subsequent frame-reuse pass

A follow-up retains stable frame hash nodes and skips invisible icon-tile QML updates while preserving springs and re-entry/model-reset behavior. Visual policy, blur bounds/suspension, native sizing and animation timings are unchanged. Serial 100-window comparisons reduce icon-mode processing median by about 30%; normal live-content and blurred-Kitty results show no consistent further speedup. See the final section of `docs/zoom-performance.md` and `docs/artifacts/frame-reuse-2026-10-30.json` for results and limits. Release build, 20 CTests, zoom-depth, the new real-pixel icons-reentry fixture and thirteen targeted scenarios pass. No full suite, live install, settings change or commit for this follow-up; the live build listed above remains installed.

After Ivan explicitly approved installation/loading, installed this pass through the dev installer with a temporary sudo-to-pkexec wrapper, then ran `tools/live.sh on` and `status`. Loaded `krellis_1030111746`; running and installed stamps match `build Oct 30 2026 11:17:51`. No settings changes or commit. Activation is verified; live visual feel remains for Ivan to assess.


## Remaining Kitty lag investigation (2026-10-30)

Ivan reports depth-dependent Kitty lag even with blur disabled; opaque Kitty is not yet tested in that live tree. Six serial 25-Kitty depth sweeps at 3440×1440 compare opaque/transparent/blurred clients and live corner/peek settings, including nested-only rounded-corner disablement. GPU timestamp-pair instrumentation and rapid wheel tests still do not reproduce the live transparency-only depth trend. Do not claim the cause is established or a further fix was made. No production/live/settings changes. Details and reproduction: docs/kitty-transparency-investigation-2026-10-30.md; summaries: docs/artifacts/kitty-transparency-2026-10-30.json.

Test helper knobs: ZOOM_LOAD_OPACITY and ZOOM_LOAD_BLUR_RADIUS override Kitty defaults; ZOOM_DEPTH_BLUR=0 leaves nested blur disabled, and ZOOM_DEPTH_SIZE controls the fixture/output dimensions. Probe KRELLIS_PROBE_GPU_TIMING=1 collects nonblocking timestamp pairs around paintScreen; GPU elapsed includes waits/submission gaps and omits unavailable trailing samples, not isolated GPU execution. drawStats counts recursive offscreen callbacks, not unique windows. New zoom-burst passes 7 checks; updated camera-coverage/output-damage pass 11/3. All six depth sweeps pass 109 assertions. No commit or live installation.


## Repeated overview scene setup corrected (2026-10-30)

Ivan now has Kitty background_opacity=1 and background_blur=0 (config reloaded on request); title-bar alpha remains separate. He reports improvement but repeated whole-desktop pauses. His roughly one-second duration is an estimate, not a measurement. The test's measured ~100 ms is not an upper bound on live stalls.

Earlier independent-Kitty tests missed the shared-process workload. ZOOM_LOAD_SINGLE_INSTANCE=1 uses a unique NEST_SOCKET instance group; zoom-burst now accepts ZOOM_BURST_COUNT/REPEATS and optional ZOOM_BURST_SHOTS. Fourteen shared-instance opaque Kitty windows at 3440×1440 reproduce repeated ~70–116 ms outward-sweep frame maxima with blur off; nested rounded-corner removal does not eliminate them. Stage timing localizes them to recreating the overview-label QML scene.

The motion overlay retains its scene/delegates while inactive, with scene opacity zero and root invisible. Active updates refresh data before showing it; hidden label counts are zero. Native/stacking changes schedule occlusion refresh only while shown. Unload still releases it. This trades some idle graphics memory for reuse; do not replace it with setVisible(false) on every navigation hide, which releases KWin offscreen graphics resources. Normal fullscreen handling is retained. MOTION appends builds/shown diagnostics. No resize, camera, blur or full-damage policy change.

Clean eight-cycle shared-instance outward maxima are 4.179–6.249 ms versus staged baseline 69.168–97.344 ms. The new overlay-reuse scenario asserts stable build counts and hidden label counts. Release build, 20 CTests, 15 targeted scenarios and standard overlay-reuse pass. Documentation/measurements: docs/zoom-performance.md final section and docs/artifacts/overlay-reuse-2026-10-30.json. No full suite, commit or live install yet. Automatic synchronous session commits remain an unproven I/O-stall candidate; they were not changed.

Final overlay-reuse endurance: 30 round trips / 60 phases / 61 assertions pass; final build's maximum measured processing span is 8.504 ms versus baseline long-run maximum 116.185 ms. Settled before/after screenshots differ at 4/1/11 of 4,953,600 pixels (initial overview / zoomed in / returned overview). No full suite, commit or live install yet; logs /tmp/krellis-overlay-endurance.log and artifact as above.

After Ivan explicitly approved installation/loading, installed the overlay scene reuse fix via the dev installer with a temporary sudo-to-pkexec wrapper, then ran `tools/live.sh on` and `status`. Loaded `krellis_1030115620`; running and installed stamps match `build Oct 30 2026 11:56:25`. No settings changes or commit. Activation is verified; live freeze/stutter improvement still needs Ivan’s feedback.


## Aurorae SVG cache pause identified and fixed locally (2026-10-30)

Ivan reports the remaining stutter primarily on outward navigation from deep layouts. The old zoom-burst test sent events 60 ms apart; Navigator's 180 ms quiet threshold accepts one step per burst. Its overlay-reuse measurements remain valid for repeated one-level transitions, not deep traversal. New deep-out asserts a camera width below .02 and at least six distinct destinations (25-window case: width .000294, 14 outward destinations). ZOOM_LOAD_FOCUS_NEW uses test-owned KWin scripting activation while opening windows to build a deep dwindle chain; optional synthetic grid text redraws on resize. Python dbus supplies persistent reply timings, excluding qdbus CLI spawn cost.

With the actual Aurorae theme available read-only, persistent compositor heartbeats catch 356–783 ms pauses outside painted-frame timing. GDB of a copied, non-capability nested KWin captures the main thread in png_write_row -> QImage::save -> KLocalImageCacheImplementation::serializeImage -> KSvg cache timer. The long operation is rendered SVG pixmap serialization, not established as disk I/O. A background session-writer experiment did not remove it and was completely reverted; session persistence is unchanged. Aurorae v2 owns the SVG objects privately/unparented; do not use private-layout pointer hacks or a global cache flush in Krellis.

The fix is a separate seven-line GPL-2.0-or-later Aurorae v6.7.5 patch disabling full-frame SVG render-cache insertion while retaining current-frame pixmaps, button caches and appearance. tools/build-aurorae-fix.sh fetches/checksums pinned source, patches and stages a uniquely named plugin; it does not install or load anything. Source stays in ignored build-aurorae-fix/. patches/README.md records licensing. Original KDE plugin is preserved. docs/aurorae-cache-fix-2026-10-30.md and docs/artifacts/aurorae-render-cache-2026-10-30.json record evidence and rollback.

Serial before/after/after/before comparisons yield worst reply delays about 783/361 ms and 778/356 ms with original Aurorae, versus <8 ms with the patch. Native redraw frames can still approach 20 ms; do not claim universal 144 Hz smoothness. The before/deep/returned screenshots differ at only 44/1/68 of 4,953,600 pixels. Hot decoration replacement preserves all 25 windows. All 20 CTests pass; patched decoration move/release, real edge resize, native resize, damage, camera coverage, urgency and close fixtures pass. Camera-coverage's hard-coded undecorated marker offsets failed identically with original Aurorae and were fixed to use actual client offsets. decorations/edge-resize now have task-owned socket .env files; edge-resize prints its successful assertion as PASS. The harness accepts optional NEST_AURORAE_THEME_DIR/NEST_DECORATION_LIBRARY only for private nested configuration. No full suite, commit or push.

Ivan explicitly authorized installation once validation completed. Installed and selected org.kde.kwin.aurorae.krellis_1030123148 using pkexec for the new file, kwriteconfig6 for the selected decoration library, then KWin reconfigure. supportInformation confirms that plugin and the unchanged __aurorae__svg__FieldworkDark theme. Source and installed library SHA256 match a5deab13c18ba3f7bfe887fd8f3c1c148465b5ae6b74833a5b9672aaedc480ec. The original org.kde.kwin.aurorae.v2 remains installed. Previous library/theme keys are recorded in build-aurorae-fix/previous-{library,theme}; revert by selecting original library and reconfiguring. This local decoration plugin needs rebuilding/reversion with relevant KDE/KDecoration upgrades.

Krellis itself remains krellis_1030115620 with matching running/installed build Oct 30 2026 11:56:25. Kitty background opacity remains 1 and blur 0; title-bar transparency is preserved. Activation is verified; live deep-zoom feel awaits Ivan's feedback.
