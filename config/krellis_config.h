#pragma once
#include <QList>
#include <QString>
#include <QVariant>

// ~/.config/krellisrc. Plain keys live in [General]; per-application rules live in [ViewRules] as
// "<app>\<key>=<value>" lines (QSettings group syntax). Both the effect and the settings module go through here so
// they cannot disagree about the file's shape.
namespace krellis::config {

// Where a new tiled window opens.
enum class SpawnTarget { Pointer, Focused, End, Dwindle };

struct Settings
{
    bool closeFocus = true;       // closing the focused window focuses its neighbour in the layout
    bool floatFirst = false;      // new windows float; drops tile only at screen edges and corners
    bool shakeFloat = true;       // shaking a window by its title bar floats it (tiling-first mode)
    SpawnTarget spawnTarget = SpawnTarget::Pointer; // beside the tile under the cursor | the focused tile | the end of the row | halve the focused tile (dwindle)
    double peek = 40;             // pixels of neighbouring tiles kept in view around a zoomed framing; 0 = off
    bool map = true;              // the minimap is available (it tucks itself away at the screen edge)
    bool mapPinned = false;       // the minimap stays out instead of sliding off the edge
    bool glide = false;           // our own animations; off = windows jump to their final rects
    QString animationProfile = QStringLiteral("trellis"); // trellis | hyprland (window presentation, no resize glides)
    QString windowAnimation = QStringLiteral("popin");    // Hyprland open/close style: popin | slide | gnomed
    bool scratchpad = false;      // the scratch layer (Meta+G) and its minimap footer; off until asked for
    bool urgent = true;           // windows that demand attention are marked, listed first in the palette and reachable with Meta+U
    bool pin = false;             // Meta+Shift+P pins a tile to the corner while it is out of view; off until asked for
    bool tabs = true;             // windows can be grouped into tabbed tiles; off disables it and splits tabbed tiles apart
    bool followTheme = true;      // overlays take the Plasma colour scheme and accent; off = the built-in palette
    bool toasts = true;           // a brief label on toggles, resize mode, undo and redo
    bool flyTo = false;           // long camera jumps zoom out, pan and zoom in (needs glide)
    bool whichKey = true;         // holding Meta+Alt shows the keyboard cheat sheet
    int whichKeyDelayMs = 400;    // how long the chord is held before the cheat sheet shows
    int tileMinWidth = 1, tileMinHeight = 1; // pixel minimum of a tile for apps without a rule; 1 = pure proportions (deep splits keep shrinking), 320x200 = the old floor
    int iconBelow = 0;            // tiles drawn narrower than this many pixels show an icon; 0 (default) = windows stay live at any size
    double landMs = 200;          // glide off: duration of the move-and-scale after a real geometry change
    bool operator==(const Settings &) const = default;
};

struct AppRule
{
    QString app;                       // desktop file name or window class
    QString placement = QStringLiteral("side");        // side | stage | floating
    QString scaling = QStringLiteral("interactive");   // interactive | inert | none
    int minWidth = 160, minHeight = 120;
    bool allowSide = true, allowStage = true, allowFloating = true;
    bool singleton = false, closable = true, scratchpad = false;
    int floatWidth = 0, floatHeight = 0;   // pixels a new float opens at; 0 = the window's own size (both 0: the default rectangle)
    bool floatAtPointer = false;           // a new float opens with its top-left corner at the pointer
    bool operator==(const AppRule &) const = default;
};

QString defaultPath();
// A file in the effect's state directory (~/.local/state/krellis). The first call also copies a pre-rename session.json over.
QString stateFile(const QString &name);

Settings load(const QString &file = defaultPath());
// Writes only the General keys, leaving rules and anything unknown in the file alone.
bool save(const Settings &settings, const QString &file = defaultPath());
// Writes one General key (the effect's live toggles).
bool saveValue(const QString &key, const QVariant &value, const QString &file = defaultPath());

QList<AppRule> loadRules(const QString &file = defaultPath());
// Replaces the whole [ViewRules] section.
bool saveRules(const QList<AppRule> &rules, const QString &file = defaultPath());

} // namespace krellis::config
