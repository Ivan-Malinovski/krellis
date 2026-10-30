# Zoom performance investigation, 2026-10-30

**Current approach:** the uniform-cover experiment below was rejected after live feedback. The latest revision requests visible clients’ destination sizes at the start of a discrete zoom and uses the original per-axis presentation. See the final sections for current behavior and measurements, including the later transparent-client blur investigation.

The first pass reduces repeated work within Krellis. It does not change native client sizing, animation settings or the full-repaint policy.

- Window and panel lookups use indices rebuilt at the start of a canvas frame. Each hit checks its index and identity, falling back to a scan after synchronous close, tab or transfer mutations. The indices never retain vector element pointers.
- Painted geometry and opacity are shared after camera/model/native updates through the rest of the frame. The cache is released at post-paint, so input and configure callbacks between frames compute fresh geometry.
- Overview labels skip clipping and QML updates outside the viewport or below their existing readable size. Inactive delegates receive one clearing update, then stop receiving updates. Cover rectangles are prepared once and filtered to the viewport.
- Minimap updates visit real layout entries rather than virtual navigation ranges, and build QVariant tile rows only after detecting a changed signature. The camera rectangle continues updating every frame.

The new `zoom-load` fixture creates 25, 50 and 100 independent Wayland windows in one quiet Qt process. It alternates overview and the deepest leaf four times, checks managed window count, checks that the camera changes and that the focused tile has positive area, and records rendering timings with the test-only paint probe. Probe timing does not request repaints or read pixels. The regular scenario suite uses 25 windows; the full load is opt-in:

```sh
cmake --build build --target krellis_paint_probe
ZOOM_LOAD_COUNTS="25 50 100" tools/scenarios.sh -j 1 -k zoom-load
```

Run timing comparisons serially, without builds or other scenarios in parallel. `NEST_BACKEND=virtual` is enforced by the runner. The fixture uses the Hyprland presentation profile, with glide/flyTo off and the minimap on. Both baseline and updated runs used the same pre-existing working-tree changes; the baseline plugin was staged before this optimization pass.

Two baseline/updated comparisons produced these median frame-processing times (milliseconds):

| Windows | Baseline, first / repeat | Updated, first / repeat |
| --- | --- | --- |
| 25 | 0.758 / 0.736 | 0.468 / 0.492 |
| 50 | 1.129 / 1.065 | 0.793 / 0.781 |
| 100 | 2.404 / 2.337 | 1.499 / 1.435 |

At 100 windows the updated 95th-percentile processing time remained about 50 ms (baseline 56–63 ms). Frame-interval tails varied substantially: the first 100-window comparison worsened from 67 to 79 ms, while the repeat improved from 113 to 85 ms. These runs support reduced median processing cost, **not a consistent frame-pacing improvement or a Dolphin/Kitty smoothness claim**.

The probe measures elapsed wall time through pre-paint, painting and post-paint, including rendering/submission and any driver waits. It does not isolate GPU execution or Krellis CPU time. Each phase can include settled frames, and frame counts vary with repaint scheduling. See the [recorded summaries](artifacts/zoom-load-2026-10-30.json) for all medians, percentiles and sample counts; raw samples and geometry snapshots are in the corresponding `/tmp/krellis-zoom-*/out` directories for this session.

Remaining candidates include offscreen native geometry updates, offscreen window painting and forced blur, and full-screen repainting. Those need separate measurement and rendering/input regression coverage. Quiet synthetic windows do not establish the cost of many independent heavy clients.

Validation: release effect build and all 20 CTest entries pass. Eleven assertion-bearing scenarios pass: `motion-feedback`, `motion-polish`, `hyprland`, `tabs`, `pin`, `map-settings`, `interrupt`, `cross-aspect`, `urgent`, `noglide-blink` and `resize`. `map` and `maximize2` completed as state-reporting scenarios. `scaled` failed its CLICK-delivery assertion; the staged pre-change baseline reproduces the same failure and fixture portal messages, so scaled-input delivery remains unverified here. Both repeat benchmark runs pass the workload/camera/positive-area assertions. Overview-label and minimap-settings screenshots were inspected. No full scenario suite, live installation or real Dolphin/Kitty load test was performed.

## Second pass: retained buffers and offscreen painting

The follow-up targets the Hyprland presentation profile used on Ivan's desktop. The Trellis profile retains its sizing behavior.

Pure camera changes no longer configure every window at every destination. Instead they retarget presentation, preserve existing buffers and coalesce native geometry updates until the move animation has settled (479 ms after the latest camera change, and never while a gesture is held). One final pass updates visible content tiles. Offscreen, icon-only, thumbnail and background-tab buffers are retained. Camera-scaled buffers use uniform aspect fitting, which can temporarily leave unused tile space until the new visible buffer commits. The input transform uses that same fitted geometry. Layout/work-area edits, native resize, profile changes and unload cancel the pending batch; normal layout sizing stays independent of navigation.

Painting skips a window when its transformed expanded rectangle, including decorations and shadows, misses the current render viewport. Culling uses the render viewport rather than the owner output so transfer flights can cross outputs. Full repaint damage remains enabled. These two changes were measured together; the results do not isolate their individual contributions.

An initial 25-window configure-cost experiment went from 25 native geometry requests to 3 for one deep focus, and from 500 requests to zero during 21 continuous pinch updates. The new test fixture also checks that a quiet held gesture remains held, wheel bursts coalesce, Meta+rectangle zoom defers geometry, visible clients land their new sizes, offscreen geometry is retained, real Wayland click/release delivery works after settling, and closing during deferral cancels the batch. `camera-resize` uses task-owned fake input and two virtual outputs and passes 17 checks.

The same serial benchmark now records actual `moveResize` calls as `nativeRequests`. The baseline is the **first-pass build installed earlier in this session**, not the older original implementation. All runs use 1280×800 virtual nested KWin, the Hyprland profile, a visible minimap and four deepest-leaf/overview round trips with 650 ms per phase:

| Workload | Geometry requests before → after | Processing p95, ms | Frame interval p95, ms |
| --- | --- | --- | --- |
| 25 quiet Qt windows | 200 → 24 | 28.405 → 1.546 | 34.018 → 17.770 |
| 50 quiet Qt windows | 400 → 24 | 43.455 → 1.528 | 66.864 → 17.619 |
| 100 quiet Qt windows | 800 → 24 | 50.553 → 2.470 | 66.893 → 17.708 |
| 25 independent Kitty processes | 200 → 24 | 7.687 → 1.218 | 23.777 → 17.640 |
| 25 Dolphin windows | 200 → 24 | 62.896 → 2.457 | 108.916 → 17.567 |
| 25 Dolphin windows, reverse-order repeat | 200 → 24 | 71.022 → 2.523 | 91.289 → 17.674 |

Kitty uses `--config NONE`, task-owned working directories and `sleep`. Dolphin opens an empty task-owned directory inside a private D-Bus session; no personal folders are seeded. The reverse-order repeat ran the updated build first, then the baseline. [Recorded summaries](artifacts/camera-navigation-2026-10-30.json) include medians and frame counts. Raw timings and geometry snapshots remain under `/tmp/krellis-{camera,kitty,dolphin}-*/out` for this session.

To reproduce real-app workloads, use `ZOOM_LOAD_APP=kitty` or `ZOOM_LOAD_APP=dolphin` together with `ZOOM_LOAD_COUNTS=25` and the normal `zoom-load` invocation. The real-app helper refuses non-test sockets. Geometry-request counts include moves and/or sizes, not client configure commits. The probe's elapsed processing time includes driver waits and does not separately measure GPU execution. Phase samples include settled frames. The virtual output's frame intervals and empty-directory/sleep workloads cannot establish behavior at Ivan's actual output refresh rates or with busy apps.

Release build and all 20 CTests pass. Thirteen assertion-bearing scenario tests pass: `camera-resize`, `hyprland`, `animation-cost`, `animation-cost-map`, `output-damage`, `cross-aspect`, `tabs`, `pin`, `resize`, `float-drag`, `motion-feedback`, `noglide-blink` and `interrupt`. The real-frame paint probe verifies scaled idle damage before/after animation and transfer on two outputs. `cross-drag` exits before its checks through the generic runner's socket guard; direct runs on test sockets fail the destination-drag assertion identically with the pre-change baseline. `resize-stage` exits early without reaching its PASS, also identically on the baseline. These two paths remain unverified. No full scenario suite, commit or live installation for the second pass yet.

Visual review: inspected 25-window screenshots during retained-buffer navigation, after the focused client resized, and after returning to overview (`/tmp/krellis-camera-visual/out`). The in-flight image is visibly enlarged/soft and aspect-fitted; the focused client fills its tile after its new buffer arrives. This is the documented presentation tradeoff, not a claim of pixel-identical native sizing throughout the animation.

After explicit installation approval, installed through the dev installer with a temporary sudo-to-pkexec wrapper, then ran `tools/live.sh on` and `status`. Loaded `krellis_1030094919`; running and installed stamps both `build Oct 30 2026 09:49:20`. Activation is verified; live smoothness still needs Ivan's feedback. No settings changes or commit.

## Follow-up: cover the tile while resizing is deferred

Ivan confirmed improved responsiveness but reported large gaps until the native resize landed. The aspect-fit presentation caused those gaps: an old narrow buffer occupied only part of a wider animated tile. Deferral still uses the same timing and visible-only policy.

Retained camera buffers now use a uniform **cover** transform, anchored at the top-left so the title bar stays in view. `visualRect` returns the full logical tile; `bufferRect` supplies the larger source-buffer transform and painting clips its excess to the tile in device coordinates. Motion snapshots, occlusion, labels and docking continue to use the logical tile. Input hit-tests the tile and maps through the buffer transform; cropped pixels outside it cannot receive a fresh press, while an existing grab still receives motion/releases outside. Popup geometry uses the same parent-buffer transform. Close animations retain the camera-cover marker, and retained markers survive structural edits while clients catch up. Floats, native resize and the lifted drag card keep their existing presentation behavior.

This fills the tile without changing the buffer's proportions. The tradeoff is temporary cropping at the bottom or right, plus the existing soft appearance while magnified. It does not make clients redraw earlier or eliminate a visible content change when the final buffer arrives. Ordinary recently transferred windows retain their existing aspect-fit treatment; camera navigation uses cover even if the transfer was recent.

`camera-coverage` samples real presented frames before any client resize, including a zoom out with a topmost wide buffer that would otherwise overpaint its neighbour. It also checks actual Wayland click/release delivery through the cropped transform. All eight checks pass. The same fixture fails on installed build `krellis_1030094919`: both in-tile sample points remain black for all nine sampled frames. On the updated build both remain at the expected client colour for all sampled frames, and neighbour pixels remain intact. See the [pixel samples](artifacts/camera-coverage-2026-10-30.json).

The pure input geometry tests independently cover uniform scale, full tile coverage, crop-coordinate inversion, popup mapping, wide buffers on portrait tiles, nonzero origins and invalid rectangles. All 20 CTest entries pass. The updated `camera-resize` fixture still passes its 17 checks, including zero native requests during held navigation and final visible-client sizing.

Serial performance checks against the installed gap build preserve the fast path: 25 empty-directory Dolphin windows retain 24 native geometry calls across eight transitions, with p95 processing 2.743 ms before and 2.672 ms after (p95 frame intervals 17.717 and 18.152 ms). The updated 100-window Qt fixture reports 24 calls, 2.925 ms processing p95 and 17.610 ms frame-interval p95. These are measurements within normal run-to-run variation, not a claim of identical timing. The coverage artifact includes the summaries.

Release build and all 20 CTests pass. Eleven assertion-bearing scenario tests pass for this follow-up: `camera-coverage`, `camera-resize`, `hyprland`, `cross-aspect`, `output-damage`, `float-close`, `close-probe`, `tabs`, `pin`, `resize` and `interrupt`. The closing/probe scenarios were rerun after retaining cover state across close/profile transitions; camera resizing and cross-aspect tests were rerun after making recent transfers use cover during navigation. No full suite, commit or live installation for this coverage fix yet.

Visual review: inspected the retained-buffer transition and settled 25-window screenshots under `/tmp/krellis-gap-visual/out`. The animated tile stays covered; the magnified buffer is temporarily soft/cropped, then the settled client fills the tile at native resolution. The prior letterbox screenshot remains under `/tmp/krellis-camera-visual/out` for comparison.

After Ivan explicitly approved installation/loading, installed the coverage fix via the dev installer with a temporary sudo-to-pkexec wrapper, then ran `tools/live.sh on` and `status`. Loaded `krellis_1030101932`; running and installed stamps both `build Oct 30 2026 10:19:34`. No settings changes or commit. Activation is verified; live appearance still needs Ivan's feedback.

## Latest revision: early visible-only sizing, direct presentation

Ivan rejected uniform-cover cropping, especially for vertical-only camera navigation: selecting the larger scale enlarged the width when only height should change, cutting off contents. The cover transform, camera-scaled flags, special popup/input mapping, crop-specific close state and geometry helper/tests have been removed. Original per-axis Hyprland presentation and shared paint/input/popup coordinates are restored. This can briefly rescale a late client buffer during an animation; it is not a promise of zero transient texture distortion.

Discrete camera destinations now submit one native geometry batch for visible content tiles immediately, then animate their available buffers with the existing independent clocks. There is no extra 479 ms native-resize delay. Offscreen, icon-only, background-tab and pinned-thumbnail buffers are retained. Each accepted wheel step has its own visible-only batch. Continuous pinch/pan gestures still submit no native geometry while held, then request the visible destination once on release. A no-op release skips geometry entirely, so stale offscreen frames are not accidentally reconfigured. Structural edits and native resizing keep their ordinary paths. Transformed expanded-bounds paint culling is unchanged.

`camera-coverage` now uses real pixel markers for the reported axis-specific case. A client-space stripe at x=100 stays fixed during a vertical-only camera change; its y=100 stripe stays fixed during a horizontal-only change. It checks immediate visible-size requests, constant native geometry along the unchanged axis, no additional requests during animation, and real Wayland click/release delivery after vertical navigation. Eleven checks pass. The updated two-output `camera-resize` has 17 checks for immediate visible-only batches, offscreen retention, held gestures, no-op release, wheel/rectangle navigation, closing and ownership. Its wait helper waits for both the pending batch and presentation flights before sampling settled input.

All 20 CTests pass. Thirteen assertion-bearing targeted scenarios pass: `camera-coverage`, `camera-resize`, `hyprland`, `animation-cost`, `animation-cost-map`, `output-damage`, `cross-aspect`, `close-probe`, `float-close`, `pin`, `tabs`, `resize` and `interrupt`. These do not establish physical desktop feel or busy-client behavior. No full suite, commit or live installation for this revision yet.

Serial comparisons against installed cover build `krellis_1030101932`, using the same 1280×800 virtual-output workload:

| Workload | Geometry calls | Processing p95 | Frame interval p95 |
| --- | --- | --- | --- |
| 25 empty-directory Dolphin windows, cover build | 24 | 2.434 ms | 17.600 ms |
| 25 empty-directory Dolphin windows, early visible sizing | 24 | 3.841 ms | 17.486 ms |
| 100 Qt windows, early visible sizing | 24 | 2.794 ms | 17.471 ms |

Earlier visible sizing has a modest processing cost in this sample; measured frame intervals remain similar. These summaries and axis pixel samples are in [camera-early-sizing-2026-10-30.json](artifacts/camera-early-sizing-2026-10-30.json). The preceding measurement caveats still apply.

Inspected matching vertical-transition screenshots: `/tmp/krellis-vertical-before/out/vertical-transition.png` shows the cover build moving/widening the cyan stripe and cropping the title to the right. `/tmp/krellis-vertical-after/out/vertical-transition.png` keeps the stripe and title width steady and contains the full title. Settled snapshots are beside them. This is a visual fixture, not a subjective smoothness guarantee.

After explicit approval, installed the early-visible-sizing revision through the dev installer with a temporary sudo-to-pkexec wrapper, then ran `tools/live.sh on` and `status`. Loaded `krellis_1030104600`; running and installed stamps both `build Oct 30 2026 10:46:01`. No settings changes or commit. Activation is verified; live visual feel still needs Ivan's feedback.


## Transparent clients: repeated deep zoom and bounded blur

Ivan reported good initial zooms but worsening motion deeper in the tree, including Kitty, and confirmed those windows are transparent/blurred. The earlier fixtures used `--config NONE` with opaque Kitty and the harness disables blur by default. An opaque Kitty depth sweep stayed near 1–2 ms p95; that workload omitted the relevant rendering cost.

The pinned local KWin 6.7 blur implementation (`third_party/kwin-src/src/plugins/blur/blur.cpp`, `BlurEffect::blur`) sizes its background textures from the entire transformed blur region before output clipping. It reallocates when the size changes. Very deep camera transforms can make a mostly offscreen tile enormous; tiny tiles can also yield zero-sized downsample levels. With 25 Kitty clients explicitly using opacity 0.8 and blur 16, real KWin blur loaded, and successive Meta+wheel inward steps, the slowdown reproduces. The baseline log records invalid-value errors and failed blur framebuffer creation.

Krellis now chooses `WindowForceBlurRole` at painting, using the current render viewport and final transform. Forced blur is withheld below 32 logical pixels on either axis or above 125% of the viewport on either axis. The Hyprland profile additionally suspends it during camera presentation flights and held navigation gestures, including popups inheriting that navigation. Normal visible tiles regain blur when motion finishes. This keeps transparent contents and the same geometry/input/resize paths; their backdrop is temporarily sharper during motion, and oversized/tiny tiles stay unblurred. There is no global blur setting change. This is a rendering fallback, not a viewport-clipped rewrite or cache of KWin's blur.

Serial 1280×800 virtual-output runs compare the installed early-sizing revision (staged before this change) with the bounded/suspended blur revision. Each uses 25 independent Kitty clients, the same dwindle layout, 650 ms per step and thirteen successive inward destinations:

| Inward step (zero-based) | Processing p95 before → after | Frame interval p95 before → after |
| --- | --- | --- |
| 3 | 17.995 → 5.742 ms | 33.025 → 17.129 ms |
| 4 | 22.635 → 1.976 ms | 33.682 → 16.968 ms |
| 5 | 26.689 → 1.803 ms | 33.726 → 17.480 ms |
| 6 | 19.715 → 2.223 ms | 33.475 → 17.099 ms |

The size bound alone reduced the worst step p95 to 15.572 ms; suspending changing blur targets during camera presentation produces the table above. Individual final blur-restoration or client-catch-up frames can still take longer (for example 12.760 ms at step 5). Samples include settled frames. Probe timings include rendering/submission and driver waits, not isolated GPU execution. These measurements do not establish physical 144 Hz desktop feel or busy-client performance. All per-step summaries are in [camera-blur-depth-2026-10-30.json](artifacts/camera-blur-depth-2026-10-30.json); raw logs/snapshots are under `/tmp/krellis-depth-wheel-{before,after,motion}/out`.

The new `zoom-depth` scenario loads real KWin blur, launches 25 task-owned transparent Kitty clients, traverses inward and outward destinations, verifies blur suspension and recovery, checks ownership and quiet held-gesture/release behavior, and rejects invalid blur GPU allocations. It passes 109 assertions. An initial fixture incorrectly expected an additional outward animation after reaching the overview limit; it now checks that the no-op leaves settled presentation intact. No effect change was needed for that fixture correction. The helper's `ZOOM_LOAD_BLUR=1` selects explicit transparent/blurred Kitty for other benchmarks; default `zoom-load` stays opaque for comparability.

Validation: release build, all 20 CTest entries and thirteen assertion-bearing scenarios pass: `zoom-depth`, `camera-coverage`, `camera-resize`, `output-damage`, `hyprland`, `cross-aspect`, `tabs`, `pin`, `close-probe`, `float-close`, `resize`, `interrupt`, `animation-cost-map`. No full suite, commit, settings change or live installation for this revision yet.

A second serial comparison at a 3440×1440 virtual output uses the same 25 blurred Kitty clients and a pointer near its lower-right corner. At inward step 3, processing p95 falls from 19.408 to 1.971 ms and interval p95 from 33.529 to 17.532 ms; step 5 goes from 17.287 to 1.747 ms /33.039 to17.130 ms. The updated step-4 maximum is still17.752 ms, so occasional catch-up/restoration frames remain. Only steps0–11 change destinations at this size; the roughly500ms no-op idle interval at step12 must not be read as a dropped-animation frame. The same artifact records every phase; raw runs are `/tmp/krellis-depth-wide-{before,after}/out`. No physical output was used.


## Further work without changing presentation (2026-10-30)

Frame window/panel indices now retain their hash nodes when the vectors and panel identities match the preceding frame. Hits still validate their vector index and retain the synchronous-mutation scan fallback. Frame visual geometry overwrites existing entries and removes departed windows instead of discarding every hash node. Geometry is still recomputed after model/native updates and the cache is still disabled outside painting. At 100 stable shown windows this avoids roughly 300 hash-node allocations and corresponding frees per animated frame.

Icon tiles keep stepping their springs offscreen but stop sending invisible QML delegate updates. A departing tile clears its pieces once; returning tiles receive fresh geometry, title, icon and occlusion pieces. Delegate model resets clear the presentation flags. Root clipping, on-screen drawing and hit-testing are unchanged. This mainly benefits the opt-in `iconBelow` mode; its default remains zero. Blur policy, camera/native sizing, scaling, transition durations and full-damage rendering are unchanged.

The new `icons-reentry` scenario uses quiet Qt clients and real-frame pixel probes to check icon cards through repeated focus/overview navigation and a float/dock model reset (seven assertions). Matching screenshots are retained with its scenario work directory. Two early fixture runs failed while parsing the theme source and negative dump dimensions; those fixture errors were corrected before the passing run. The older `icons` fixture exits before its assertions because it expects all nine windows to meet its icon threshold while its current layout produces only five icon tiles; it is not counted as passing coverage.

Release build, all 20 CTests, `zoom-depth` (109 checks), `icons-reentry` (seven checks) and thirteen existing targeted scenarios pass: camera-coverage, camera-resize, output-damage, hyprland, cross-aspect, tabs, pin, close-probe, float-close, resize, interrupt, animation-cost-map and motion-polish. No full suite, live installation, settings change or commit. Existing worktree changes were preserved.

Eight serial comparisons use 100 quiet Qt windows, the same Hyprland/minimap configuration and 1280×800 virtual output. Run order is before/after/after/before for each mode, with no compilation or other task-owned scenarios running:

| Mode | Processing median before → after, ms | Processing p95 before → after, ms |
| --- | --- | --- |
| Normal live contents, first pair | 0.715 → 0.691 | 3.035 → 2.831 |
| Normal live contents, reverse-order pair | 0.694 → 0.699 | 2.447 → 2.784 |
| Icon threshold 160, first pair | 1.630 → 1.140 | 3.539 → 2.904 |
| Icon threshold 160, reverse-order pair | 1.638 → 1.151 | 3.505 → 3.119 |

Icon-mode median processing improves about 30% in both comparisons, with a smaller p95 improvement. Normal live-content results overlap: there is no clear general navigation speedup, and the blurred-Kitty depth recheck also shows no consistent gain. Frame-interval p95 stays near 17.3–17.6 ms. All icon runs issue three geometry calls; normal runs issue 24 except the first baseline's 64. This pass changes no native geometry code. A preliminary baseline overlapped compilation and was discarded. These measurements cover compositor processing including driver waits, not isolated GPU execution or physical-display smoothness. Full summaries and caveats: [frame-reuse artifact](artifacts/frame-reuse-2026-10-30.json). Raw serial logs: `/tmp/krellis-further-{normal,icons}-*/out`; regressions: `/tmp/krellis-further-regressions.log`, with corrected pixel fixture at `/tmp/krellis-scn.mYE1Cq/icons-reentry/out`.

The same icons-reentry fixture also passes all seven checks against the staged pre-change plugin (`/tmp/krellis-further-icons-pixels/out`). Comparing the three settled baseline/updated 1280×800 screenshots finds respectively 4, 9 and 5 differing pixels out of 1,024,000; they are near-identical, not byte-identical. Updated screenshots were visually inspected.

After Ivan explicitly approved installation/loading, installed this pass through the dev installer with a temporary sudo-to-pkexec wrapper, then ran `tools/live.sh on` and `status`. Loaded `krellis_1030111746`; running and installed stamps match `build Oct 30 2026 11:17:51`. No settings changes or commit. Activation is verified; live visual feel remains for Ivan to assess.


## Repeated overview scene creation: shared-instance Kitty (2026-10-30)

After making Kitty's terminal background opaque, Ivan reported some improvement but remaining whole-desktop pauses during repeated zooming. The roughly one-second duration was a subjective estimate, not a timed measurement; the nested measurements below are not an upper bound on what he experienced. Title-bar transparency belongs to the decoration and was not changed.

A gap in the preceding workload mattered: the live Kitty windows share one process, while the earlier fixtures launched independent processes. `ZOOM_LOAD_SINGLE_INSTANCE=1` now uses a task-socket-specific Kitty instance group, never the live group. A serial 14-window, 3440×1440 test with opaque backgrounds and blur disabled reproduces repeated CPU processing maxima of about 70–116 ms on outward sweeps across 30 round trips. GPU timeline spans stay much smaller. Disabling rounded corners only in the nested compositor leaves similar pauses. Per-frame stage timing and temporary per-canvas probes localize most of each pause to `updateMotionOverlay`, specifically scene setup after its preceding destruction. Cold minimap setup also costs time but is not the repeated overview-return path. Temporary production profiling was removed afterward; test-probe stage timing remains available.

The label overlay now retains its QML scene, graphics context and delegates while unused. Scene opacity immediately hides the last buffer and the QML root becomes invisible; active updates restore current geometry, titles, occlusion and feedback before restoring opacity. Hidden overlays no longer schedule label refreshes from native geometry/stacking changes. Unload still destroys the scene; fullscreen retains its existing overlay hiding/resource behavior. `MOTION` diagnostics append `builds` and `shown`; label counts are zero while the root is hidden. No camera timing, native resize batching, blur policy, window scaling, transparency or full-damage flags changed. Keeping resources warm uses more idle memory than discarding them; one scene per output is retained, with its output-sized render buffer.

A clean final build's eight serial shared-instance round trips show outward-sweep processing maxima 4.179–6.249 ms, versus 69.168–97.344 ms in the five-cycle staged comparison. Inward maxima are 3.546–6.055 ms versus 11.822–16.743 ms. These are observed maxima over different-length runs, not a promise for physical 144 Hz or busy terminals. Cold scene creation remains; this removes repeated creation. The new `overlay-reuse` scenario asserts ownership, frame sampling, stable scene-build counts and hidden label counts across focus/overview cycles. A longer repeat and before/after screenshots are recorded in the accompanying artifact.

Release build, all 20 CTests and fifteen targeted scenarios pass: motion-polish, motion-feedback, urgent, icons-reentry, camera-coverage, camera-resize, output-damage, cross-aspect, tabs, pin, close-probe, float-close, resize, interrupt and animation-cost-map. Motion/overview screenshots were inspected. The updated overlay-reuse fixture passes seventeen checks in its standard eight-cycle run. No full suite or commit. The fix is not installed live yet.

[Measurement artifact](artifacts/overlay-reuse-2026-10-30.json). Main logs: `/tmp/krellis-overlay-{final-build,ctest,regressions,final-stress}.log`. Reproduce the shared-process stress run with `ZOOM_DEPTH_SIZE="3440 1440" ZOOM_BURST_REPEATS=30 KRELLIS_PROBE_GPU_TIMING=1 tools/scenarios.sh -j 1 -k overlay-reuse`.

An independent inspection found automatic session commits perform synchronous disk I/O on the compositor thread. The test harness stores state on tmpfs, whereas the real session is on btrfs. Five isolated 16 KiB write/fsync probes took 3–17 ms amid high system I/O pressure; they did not reproduce a second-long commit. Session writing is unchanged in this patch and remains a follow-up candidate if pauses persist. Do not attribute the live freeze to it without further evidence.


The final clean-build endurance run completes 30 inward/outward round trips (60 phases, 61 assertions) with a maximum recorded frame-processing span of 8.504 ms. The staged baseline's corresponding longer run reached 116.185 ms, with repeated outward peaks above 70 ms. Exact same-cycle pairing is not claimed. Separate settled screenshot comparisons at 3440×1440 differ by 4 pixels initially, 1 pixel while zoomed in and 11 pixels after returning to overview, each out of 4,953,600 pixels. These verify near-identical fixtures, not every possible live scene. The temporary frame-profile code is absent from the final build. Endurance log: `/tmp/krellis-overlay-endurance.log`; pixels: `/tmp/krellis-overlay-pixel-{before,after}/out`. The live effect is still the earlier installed build pending Ivan's approval.

After Ivan explicitly approved installation/loading, installed the overlay scene reuse fix via the dev installer with a temporary sudo-to-pkexec wrapper, then ran `tools/live.sh on` and `status`. Loaded `krellis_1030115620`; running and installed stamps match `build Oct 30 2026 11:56:25`. No settings changes or commit. Activation is verified; live freeze/stutter improvement still needs Ivan’s feedback.


## Aurorae SVG cache pause identified and fixed locally (2026-10-30)

Ivan reports the remaining stutter primarily on outward navigation from deep layouts. The old zoom-burst test sent events 60 ms apart; Navigator's 180 ms quiet threshold accepts one step per burst. Its overlay-reuse measurements remain valid for repeated one-level transitions, not deep traversal. New deep-out asserts a camera width below .02 and at least six distinct destinations (25-window case: width .000294, 14 outward destinations). ZOOM_LOAD_FOCUS_NEW uses test-owned KWin scripting activation while opening windows to build a deep dwindle chain; optional synthetic grid text redraws on resize. Python dbus supplies persistent reply timings, excluding qdbus CLI spawn cost.

With the actual Aurorae theme available read-only, persistent compositor heartbeats catch 356–783 ms pauses outside painted-frame timing. GDB of a copied, non-capability nested KWin captures the main thread in png_write_row -> QImage::save -> KLocalImageCacheImplementation::serializeImage -> KSvg cache timer. The long operation is rendered SVG pixmap serialization, not established as disk I/O. A background session-writer experiment did not remove it and was completely reverted; session persistence is unchanged. Aurorae v2 owns the SVG objects privately/unparented; do not use private-layout pointer hacks or a global cache flush in Krellis.

The fix is a separate seven-line GPL-2.0-or-later Aurorae v6.7.5 patch disabling full-frame SVG render-cache insertion while retaining current-frame pixmaps, button caches and appearance. tools/build-aurorae-fix.sh fetches/checksums pinned source, patches and stages a uniquely named plugin; it does not install or load anything. Source stays in ignored build-aurorae-fix/. patches/README.md records licensing. Original KDE plugin is preserved. docs/aurorae-cache-fix-2026-10-30.md and docs/artifacts/aurorae-render-cache-2026-10-30.json record evidence and rollback.

Serial before/after/after/before comparisons yield worst reply delays about 783/361 ms and 778/356 ms with original Aurorae, versus <8 ms with the patch. Native redraw frames can still approach 20 ms; do not claim universal 144 Hz smoothness. The before/deep/returned screenshots differ at only 44/1/68 of 4,953,600 pixels. Hot decoration replacement preserves all 25 windows. All 20 CTests pass; patched decoration move/release, real edge resize, native resize, damage, camera coverage, urgency and close fixtures pass. Camera-coverage's hard-coded undecorated marker offsets failed identically with original Aurorae and were fixed to use actual client offsets. decorations/edge-resize now have task-owned socket .env files; edge-resize prints its successful assertion as PASS. The harness accepts optional NEST_AURORAE_THEME_DIR/NEST_DECORATION_LIBRARY only for private nested configuration. No full suite, commit or push.

Ivan explicitly authorized installation once validation completed. Installed and selected org.kde.kwin.aurorae.krellis_1030123148 using pkexec for the new file, kwriteconfig6 for the selected decoration library, then KWin reconfigure. supportInformation confirms that plugin and the unchanged __aurorae__svg__FieldworkDark theme. Source and installed library SHA256 match a5deab13c18ba3f7bfe887fd8f3c1c148465b5ae6b74833a5b9672aaedc480ec. The original org.kde.kwin.aurorae.v2 remains installed. Previous library/theme keys are recorded in build-aurorae-fix/previous-{library,theme}; revert by selecting original library and reconfiguring. This local decoration plugin needs rebuilding/reversion with relevant KDE/KDecoration upgrades.

Krellis itself remains krellis_1030115620 with matching running/installed build Oct 30 2026 11:56:25. Kitty background opacity remains 1 and blur 0; title-bar transparency is preserved. Activation is verified; live deep-zoom feel awaits Ivan's feedback.
