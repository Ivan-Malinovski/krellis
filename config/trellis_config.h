#pragma once
#include <QList>
#include <QString>
#include <QVariant>

// ~/.config/trellisrc. Plain keys live in [General]; per-application rules live in [ViewRules] as
// "<app>\<key>=<value>" lines (QSettings group syntax). Both the effect and the settings module go through here so
// they cannot disagree about the file's shape.
namespace trellis::config {

struct Settings
{
    bool floatFirst = false;      // new windows float; drops tile only at screen edges and corners
    bool placeAtPointer = true;   // a new tiled window opens beside the tile under the cursor
    double peek = 40;             // pixels of neighbouring tiles kept in view around a zoomed framing; 0 = off
    bool mapPinned = false;       // the minimap stays out instead of sliding off the edge
    bool glide = false;           // our own animations; off = windows jump to their final rects
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
    bool singleton = false, closable = true;
    bool operator==(const AppRule &) const = default;
};

QString defaultPath();

Settings load(const QString &file = defaultPath());
// Writes only the General keys, leaving rules and anything unknown in the file alone.
bool save(const Settings &settings, const QString &file = defaultPath());
// Writes one General key (the effect's live toggles).
bool saveValue(const QString &key, const QVariant &value, const QString &file = defaultPath());

QList<AppRule> loadRules(const QString &file = defaultPath());
// Replaces the whole [ViewRules] section.
bool saveRules(const QList<AppRule> &rules, const QString &file = defaultPath());

} // namespace trellis::config
