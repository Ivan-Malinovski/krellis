# Krellis

An infinite-canvas tiling desktop for KDE Plasma 6 on Wayland, written as a KWin effect.

Your windows live in a tiled layout on a canvas much bigger than the screen. A camera looks at part of it, zooming and panning (with spring motion if you turn on `glide`). You can focus on one window, zoom out to see them all on a minimap, drag windows around to re-dock them, and still float anything you like on top.

Krellis is a port of the "desktop" demo of [DanFessler/trellis](https://github.com/DanFessler/trellis) to KWin (see [Credits](#credits)). The name is KDE + Trellis.

> **Status: 0.0.1, early.** It runs daily on one machine (CachyOS, Plasma 6.7.5, two monitors). Expect rough edges, and read [Safety](#safety) before enabling it.

## Features

- **Tiled layout on an infinite canvas.** Windows dock into rows and columns; a camera zooms and pans over them, with spring physics when `glide` is on. One canvas per monitor.
- **Drag to re-dock.** Drop a window on another one to split it, on a screen edge to dock there, or on a seam between tiles. Shift-drag a title bar to float it.
- **Floating windows.** Float any window, or switch to floating-first mode where new windows float and only edge and corner drops tile. Floats snap to screen edges and corners.
- **Keyboard movement and focus.** Move the focused tile through the layout with Meta+arrows (or Meta+A/S/D/F), focus neighbours with Meta+Ctrl+arrows, resize with the arrow keys. A tile at the edge of a monitor moves on to the next monitor.
- **Minimap.** Shows the whole canvas, tucks itself away at the screen edge, and lets you drag a square to zoom to those tiles.
- **Saved views, undo and redo** for layout edits, and **session restore**: the layout comes back after a login, matched by app id and window role.
- **Edge drawers and a scratchpad.** Stash windows in a drawer at a screen edge; mark windows as scratch windows that come and go together with Meta+G (the scratchpad is opt-in).
- **Per-application rules**: placement, scaling, minimum size, singleton, closability.
- **Follows your Plasma colour scheme and accent colour.**
- **Settings module** in System Settings, and a built-in cheat sheet (hold Meta+Alt).

## Requirements

- KDE Plasma **6.7** on Wayland. The effect uses KWin's private C++ API, so it is **locked to the KWin it was built against**. Rebuild after every KWin upgrade (`./install.sh` again). Only 6.7.x has been tried.
- A C++20 compiler, CMake 3.20+, and Ninja or Make.
- Development packages for KWin and its dependencies: KWin, KDecoration3, Qt 6 (Gui, Widgets, Quick, DBus), KDE Frameworks 6 (CoreAddons, GlobalAccel, ColorScheme, Config, and for the settings module KCMUtils, I18n, Kirigami), extra-cmake-modules, and the Vulkan headers (KWin's CMake config asks for them).

On Arch and CachyOS, where this was developed:

```sh
sudo pacman -S --needed base-devel cmake ninja extra-cmake-modules vulkan-headers kwin kdecoration kcmutils
```

Other distributions need the equivalent `-dev`/`-devel` packages (on Debian and Ubuntu `kwin-dev`, on Fedora `kwin-devel`); I have not built it there. If `./install.sh` fails at configure time, the CMake error names the missing package.

## Install

```sh
git clone https://github.com/Ivan-Malinovski/krellis.git
cd krellis
./install.sh            # builds, then installs to /usr with sudo
```

Then switch it on in **System Settings → Window Management → Desktop Effects → Krellis**, or run `./install.sh --enable`. It is not enabled by default, and installing does not enable it.

If an older Krellis was already running, log out and back in: KWin keeps a plugin file it has loaded for the rest of the session, so the new build does not load until then.

`./install.sh --help` lists the options (`--prefix`, `--no-kcm`, `--enable`, `--uninstall`). Extra CMake arguments go in `CMAKE_ARGS`. KWin only looks for effects under the system Qt plugin path, so keep the default prefix unless you know your KWin searches elsewhere.

### Uninstall

```sh
./install.sh --uninstall
```

This removes the plugin and the settings module and disables the effect. Your settings (`~/.config/krellisrc`) and saved layouts (`~/.local/state/krellis`) stay; delete them by hand if you want them gone.

## Keyboard

Krellis registers its shortcuts as KDE global shortcuts, so you can change any of them in **System Settings → Shortcuts** (look for "Krellis").

| Keys | Action |
|---|---|
| Meta + arrows, or Meta + A / S / D / F (left / down / up / right) | Move the focused panel that way |
| Meta+Ctrl + arrows | Focus the tile in that direction |
| Meta+Ctrl+R | Resize with the arrow keys |
| Meta + `[` / `]` | Previous / next panel |
| Meta+Alt + Up / Down | Overview / step out |
| Meta+Alt + Left / Right | Back / forward through camera history |
| Meta+Shift+Return | Maximize panel |
| Meta+Shift+F | Float / tile panel |
| Meta+Shift+X | Stash the window in an edge drawer |
| Meta+Shift+T | Flip stacked / side by side |
| Meta+Shift+H | Hide panel to the dock |
| Meta+Shift+D | Dock beside the stage |
| Meta+Shift+W | Close panel |
| Meta+G / Meta+Shift+G | Show or hide the scratchpad / mark a window as scratch (scratchpad must be enabled) |
| Meta+Alt+F | Toggle floating-first mode |
| Meta+Alt+B / M / S | Toggle bar / toggle map / save view |
| Meta+Alt+Z / Meta+Alt+Shift+Z | Undo / redo a layout edit |
| **Meta+Alt+Escape** | **Turn Krellis off** |

Holding Meta+Alt shows the full cheat sheet.

Moving a panel: it behaves as if you dropped it in the middle of the neighbour. Beside a wide tile, under or over a tall one. Two tiles that are all there is to their split swap places. At the edge of a monitor the tile moves to the monitor next to it, and when there is none it shrinks by 20%.

**Shortcut conflicts.** Plasma already uses some of these: Meta+arrows are Quick Tile, Meta+D is Show Desktop, Meta+A may switch activities. KDE keeps a shortcut you have stored over a new default, so Krellis will not take them on its own. Free them under System Settings → Shortcuts, then assign the Krellis ones.

## Mouse

| Gesture | Action |
|---|---|
| Alt / Meta + scroll | Zoom a column and row |
| Scroll on a title bar | Zoom |
| Ctrl+Alt + drag | Pan |
| Meta + drag | Zoom to a rectangle |
| Drag a square on the minimap | Zoom to the tiles it touches |
| Shift + drag a title bar | Float the window (inverts the mode) |

## Configuration

Use the settings module (System Settings → Window Management → Krellis, or the Configure button of the effect), or edit `~/.config/krellisrc`. Settings go under `[General]`; per-app rules go under `[ViewRules]`. The effect reloads them when the module saves.

| Key | Default | Meaning |
|---|---|---|
| `closeFocus` | `true` | Closing the focused window focuses its neighbour in the layout |
| `floatFirst` | `false` | New windows float; drops tile only at screen edges and corners |
| `spawnTarget` | pointer | Where a new window opens: beside the tile under the pointer, the focused tile, or the end of the row |
| `peek` | `40` | Pixels of neighbouring tiles kept in view around a zoomed framing (0 = off) |
| `map` | `true` | Minimap available |
| `mapPinned` | `false` | Keep the minimap out instead of sliding it off the edge |
| `glide` | `false` | Spring camera and animated layout changes. Off, windows go straight to their new rectangles and play a short move-and-scale (`landMs`, default 200 ms) |
| `scratchpad` | `false` | The scratch layer and its shortcuts |
| `followTheme` | `true` | Overlays use the Plasma colour scheme and accent |
| `toasts` | `true` | Brief labels on toggles, undo and redo |
| `flyTo` | `false` | Long camera jumps zoom out, pan and zoom in (needs `glide`) |
| `whichKey` | `true` | Holding Meta+Alt shows the cheat sheet (`whichKeyDelayMs`, default 400) |
| `tileMinWidth`, `tileMinHeight` | `1` | Pixel minimum of a tile for apps without a rule |
| `iconBelow` | `0` | Tiles narrower than this show an icon instead of the live window; 0 keeps windows live at any size |

## Safety

An effect that crashes takes the compositor down with it, and every Wayland app goes with it. Krellis loads with that in mind:

- **Meta+Alt+Escape** turns it off at any time and puts windows back at their normal geometry.
- A crash-loop breaker refuses to load the effect after it has taken KWin down twice in a row soon after loading, and sends a notification, so you can still log in.
- The plugin is tied to the exact KWin it was built against. After a Plasma update, reinstall before logging in again.

It has been exercised in a nested KWin and on one real desktop. Hardware input, fractional scaling and unusual monitor layouts have had little testing.

## Building and testing

The layout model is a pure C++ library with no Qt GUI or KWin dependency, so it builds and tests anywhere:

```sh
cmake -S . -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core && ctest --test-dir build-core --output-on-failure
```

The effect and the settings module need the KWin development files (`-DKRELLIS_BUILD_EFFECT=ON`). Scenario scripts in `scenarios/` drive the effect inside a nested KWin that never touches your session; `tools/nested.sh` runs one. [`AGENTS.md`](AGENTS.md) describes the architecture, the harness, and the many gotchas. Design notes are in [`docs/plans`](docs/plans) and [`docs/dev`](docs/dev).

## Credits

Krellis is a **clean-room port** of the behaviour of [DanFessler/trellis](https://github.com/DanFessler/trellis) (infinite-canvas desktop, springs, docking, minimap). It follows that project's semantics and contains none of its code, because the original is under a noncommercial license. Krellis is not affiliated with or endorsed by its author.

## License

[MIT](LICENSE)
