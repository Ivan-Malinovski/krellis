# Hyprland window animation profile

Select **Animation profile → Hyprland** in System Settings → Window Management → Krellis, or set:

```ini
[General]
animationProfile=hyprland
windowAnimation=popin
```

The settings module reloads the effect when saving. After editing the file manually, call the effect's `reloadSettings` D-Bus method. `animationProfile=trellis` returns to the existing behavior. The default remains `trellis`.

The Hyprland profile overrides `glide`, `flyTo`, and `landMs` without overwriting their saved values. The camera goes directly to its destination. Window motion runs independently, including when `glide=false` and `landMs=0`. Native interactive resize and the lifted drag card continue to follow the pointer directly; preview neighbours animate between their destinations.

The reference is Hyprland **v0.54.0**, specifically its [example configuration](https://github.com/hyprwm/Hyprland/blob/v0.54.0/example/hyprland.conf) and [desktop animation implementation](https://github.com/hyprwm/Hyprland/blob/v0.54.0/src/managers/animation/DesktopAnimationManager.cpp). Pinning this preset avoids silently changing behavior when upstream defaults change.

| Property | Duration | Curve |
|---|---|---|
| Move / rearrange | 479 ms | Cubic Bézier `(0.23, 1, 0.32, 1)` |
| Open geometry | 410 ms | Cubic Bézier `(0.23, 1, 0.32, 1)` |
| Close geometry | 149 ms | Linear |
| Fade in | 173 ms | Cubic Bézier `(0.5, 0.5, 0.75, 1)` |
| Fade out | 146 ms | Cubic Bézier `(0.5, 0.5, 0.75, 1)` |

Hyprland's speed units are 100 ms; these correspond to 4.79, 4.1, 1.49, 1.73, and 1.46. Its named `easeOutQuint` is a Bézier curve and differs from Krellis's polynomial `easeOutQuint` function. The existing Bézier solver evaluates the preset numerically; this is behavioral compatibility, not bitwise floating-point compatibility with upstream.

`windowAnimation` chooses the opening and closing geometry:

- `popin`: 87% of the destination size, centered, with a five-pixel floor limited by the destination size.
- `slide`: preserve size and offset toward the output edge nearest the destination's center. The calculation includes the output's logical origin. As in the reference, horizontal offsets are one window width; vertical offsets reach the output's top or bottom.
- `gnomed`: preserve width and collapse height toward the vertical center.

Changing a geometry destination captures the current presentation and restarts that geometry clock. Its independent fade keeps running. Closing during a transition captures the painted rectangle and opacity before removing the window from the model, and retains the deleted-window reference until the close completes. Existing hide/restore and cross-output flight bookkeeping is retained; hide/restore use the preset clocks with Krellis's dock destinations rather than claiming a Hyprland minimize equivalent.

For layout edits, KWin receives the destination geometry rather than intermediate animation sizes. Client configure acknowledgements cannot restart the presentation clock. The paint transform and input mapping share the same rectangle. This avoids continuous client relayout during a glide, but may temporarily scale an old buffer or contents already laid out at the destination. Ordinary layout flights keep the profile's direct per-axis scaling; transferred windows temporarily preserve their aspect while their clients catch up.

For camera-only navigation, a discrete destination change requests native geometry once for visible content tiles immediately, before the presentation animation completes. Offscreen, icon-only, pinned-thumbnail and background-tab buffers are retained. Each accepted wheel step has its own visible-only destination batch. Continuous pinch/pan gestures retain buffers until release, then submit one visible-only batch; a quiet held gesture never ends on a timer. The original direct per-axis presentation is used, with no uniform cover transform or content cropping. Vertical-only camera changes therefore keep the horizontal scale unchanged. A late client can still show temporarily rescaled contents or a visible content update on commit. Structural/work-area edits, native resizing, profile changes and unload cancel a pending gesture batch. A wall-clock timer can finish a released batch even when rendering is idle. `CAMERA-RESIZE` in dump reports pending state and cumulative camera-change/culling counts.

Backdrop blur is suspended during Hyprland camera presentation flights and held navigation gestures, including popups inheriting that navigation, then restored at rest. Transparency and the paint/input rectangles are unchanged; the backdrop can look sharper during motion. Forced blur is also withheld when the painted frame is under 32 logical pixels on either axis or exceeds 125% of the current render viewport on either axis. This applies per viewport, including cross-output and closing presentation. KWin 6.7 allocates blur targets for the full transformed region before clipping, so an offscreen portion can otherwise dominate allocation and rendering. Small regions can produce zero-sized downsample targets. There is no global blur-setting change. `dump` window rows include the current `blur` force flag.

Painting also skips windows whose transformed expanded geometry (including shadows) misses the current render viewport. Transfer flights are tested against each render viewport so they can cross output boundaries. Full damage and transformed-window flags remain enabled; this is not a partial-repaint optimization. The [performance investigation](zoom-performance.md) records measurements and limitations.

Painting a flight does not rescan native geometry. Layout edits, pointer motion, native resize and settled drag targets refresh it explicitly; a camera snapshot detects navigation changes that arrive without a layout edit. The profile also skips native minimum-size and aspect-fit calculations when sampling its painted rectangles, since it does not use those constraints. Each canvas advances on its own output's render view. Ordinary repaint requests, including minimap and other overlay updates, go directly to that output's views in device coordinates: KWin's general region repaint API also schedules disjoint views. Cross-output flights retain full repaint requests through the final cleanup. The screen-level transformed-window flag and full damage within a rendered output remain enabled for correct partial client damage.

`PAINT-STATS` in the D-Bus dump counts canvas frame, native geometry and motion-target passes. In a 250 ms two-output nested fixture, the original path performed 30 geometry passes, 60 motion scans, and 31 idle-output canvas passes. After the optimization the same fixture recorded zero for those three counts, while the moving output rendered 16 frames and sent no further native configure requests. These counts measure avoided work, not real-app frame pacing or an FPS increase. `animation-cost.sh` checks the counters; `animation-cost-map.sh` repeats it with visible, pinned minimaps. `output-damage.sh` uses the real-frame paint probe to check scaled partial client damage on both outputs before and after animations and transfers. Build `krellis_paint_probe` explicitly before running the latter.

This ports the window preset and the three geometry styles. It does not implement workspace transitions, Hyprland border/shadow/color animations, arbitrary Hyprland configuration imports, per-app animation rules, forced slide directions, or newer upstream spring curves. Existing popup behavior remains separate. KWin and Hyprland also have different decorations, rendering and scheduling; identical pixels or subjective smoothness have not been established.

`tests/test_motion.cpp` checks known parametric Bézier points and style geometry, including nonzero/negative output origins. `scenarios/hyprland.sh` uses Qt clients in a two-output virtual nested KWin to check the independent clocks, reflow without repeated resize requests, reversals, style changes, close references and output transfer. These checks do not replace real-app frame-pacing measurements.

The adapted window-style behavior and preset retain Hyprland's [BSD-3-Clause notice](../LICENSES/Hyprland-BSD-3-Clause.txt), also installed in `share/licenses/krellis`. The rest of Krellis retains its MIT license.
