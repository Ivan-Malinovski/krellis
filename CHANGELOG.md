# Changelog

## Unreleased

- Dropping on a window now splits only that tile into halves, including drops deep inside its body. Dropping in the gap between tiles still shares space across the row or column. The title strip still accepts tabs.
- Fixed: a window floated with Shift and dragged to another monitor could vanish from Krellis (never tiled again until it was closed and reopened). Handing a window to another monitor judged it a second time, and a floating window that KWin briefly reports as undecorated looked like an always-on-top overlay. Windows that are already managed are no longer re-judged when they move between monitors.
- Pressing Shift in the middle of a drag now floats the window straight away, without moving the pointer (and releasing it undoes that). Before, the key was only noticed on the next pointer motion, so pressing it while holding still seemed to do nothing.
- **Scroll on the gap between two tiles to zoom**, with no key held and whatever the windows are (Brave and other apps that draw their own title bar included). Scroll in narrows the frame toward the tile on the right or lower side of the gap, scroll out widens it; hold Shift to favour the left or upper tile instead. Scrolling over a window is still left to the app.
- Small helper windows no longer take a tile or get resized: fixed-size windows (password and polkit prompts such as the one `pkexec` shows, the wallet prompt), windows with a parent or that skip the taskbar, and undecorated always-on-top or thin strips (screen-sharing "you are sharing" bars) keep their own size and place. Decided from how the window asks to be shown, not from its name; a small, ordinary, resizable window still tiles. The journal line `leaving alone <app> <title> <reason>` says why.
- Plasma's own windows (the panel edit toolbar and panel settings, the widget explorer) are never tiled, so editing panels no longer fights the layout.
- Windows keep KWin's background blur. Krellis draws every tile transformed, which made KWin's blur effect skip them; tiles now carry the force-blur flag, so translucent windows (Kvantum, Kitty, Dolphin) blur what is behind them again.
- **Survives KWin upgrades.** The plugin records the KWin it was built for and refuses to load into any other, logging why and sending one notification, instead of crashing the compositor on a changed private ABI. `KRELLIS_ALLOW_KWIN_MISMATCH=1` overrides that.
- `./install.sh --check` says whether the installed plugin matches the installed KWin. `--build-only` and `--install-only` split building from installing. The installer now removes the old plugin file before installing, so a running KWin never has a mapped file overwritten under it.
- `./install.sh --pacman-hook[=warn|rebuild]` (Arch and derivatives) installs a hook that runs after pacman upgrades `kwin`: `warn` prints what to do, `rebuild` rebuilds from your checkout as your user and installs it. `--no-pacman-hook` removes it.

- **Quick jump (Meta+/).** A palette over your windows and saved views: type part of a title or app name (`ff docs`, `term`), Up/Down or Tab to choose, Return to fly the camera there and focus it. Hidden, scratchpad and drawer windows are brought back; windows on other monitors are listed too. Empty query plus Return returns to the previous window. Esc, a click outside, or the shortcut again closes it. Rebind it in System Settings → Shortcuts ("Krellis: Quick Jump").
- **Pin (Meta+Shift+P), off by default.** Turn it on with the Pin checkbox in the settings module or `pin=true`. A pinned tile keeps its place in the layout, and while it is out of view its window is drawn live as a thumbnail in the bottom-left corner. Click the thumbnail to go to the tile; press the shortcut again to unpin. Pins are not saved across sessions, and floating windows cannot be pinned (they already stay on screen).
- A stashed (edge-drawer) window that something outside Krellis activates, such as the start menu, a launcher or the app itself, now slides out instead of staying off screen. Activating a tile that is out of view already brought it into view.
- **Tabbed tiles (Meta+PageUp/PageDown), on by default.** Turn them off with the Tabbed tiles checkbox in the settings module or `tabs=false`. Several windows can share one tile: drop a window on the title bar of a tile (its top strip; the body splits the tile), or press Meta+Ctrl+Shift+arrow to group the focused window into its neighbour. A tab strip appears along the top; click a tab to show it, middle-click to close it, drag it out to give it a tile of its own (or Meta+Shift+U). The bare part of the strip drags the tab on show, like a title bar. Meta+PageDown / Meta+PageUp cycle the tabs. Turning `tabs` off splits every tabbed tile back into separate tiles. Tabs are not saved across sessions, and a tile under an overlay float shows no strip.

## 0.0.2

- A tiled window that goes fullscreen (a Brave video, a game) now covers the whole output instead of being scaled into its tile, gets native input, and returns to its tile afterwards. The overlays (bar, minimap, toasts) are hidden while a window is fullscreen.

## 0.0.1

First tagged release.

- Tiled layout on an infinite canvas with a spring-driven camera, one canvas per monitor.
- Drag docking, floating windows, floating-first mode, edge drawers, an opt-in scratchpad, minimap, saved views, undo and redo.
- Keyboard movement (Meta+arrows or Meta+A/S/D/F) and focus (Meta+Ctrl+arrows), with hand-over to the next monitor and a 20% shrink at a dead end.
- Session restore, per-application rules, Plasma theme following, and a settings module.
- `install.sh` builds and installs the effect and the settings module; `--uninstall` removes them.
