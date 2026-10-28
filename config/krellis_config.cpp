#include "krellis_config.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>
#include <algorithm>
#include <cmath>

namespace krellis::config {

QString defaultPath()
{
    const QString file = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/krellisrc");
    // Krellis was called Trellis until 2026-10-22: take the old settings along once, and leave the old file alone.
    static const bool migrated = [&file] {
        const QString legacy = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/trellisrc");
        if (!QFile::exists(file) && QFile::exists(legacy))
            QFile::copy(legacy, file);
        return true;
    }();
    Q_UNUSED(migrated)
    return file;
}

QString stateFile(const QString &name)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    const QString file = base + QStringLiteral("/krellis/") + name;
    // The saved layout (session.json) came from kwin-trellis's state directory; copy it over once. The crash-loop
    // marker is deliberately not carried over, so old strikes cannot keep the renamed effect off.
    static const bool migrated = [&base, &file] {
        const QString legacy = base + QStringLiteral("/kwin-trellis/session.json");
        const QString target = base + QStringLiteral("/krellis/session.json");
        if (!QFile::exists(target) && QFile::exists(legacy)) {
            QDir().mkpath(base + QStringLiteral("/krellis"));
            QFile::copy(legacy, target);
        }
        return true;
    }();
    Q_UNUSED(migrated)
    return file;
}

Settings load(const QString &file)
{
    // Qt's INI reader files [General] keys under their bare names, so "General/key" never matches on read.
    const QSettings c(file, QSettings::IniFormat);
    Settings s;
    s.closeFocus = c.value(QStringLiteral("closeFocus"), s.closeFocus).toBool();
    s.floatFirst = c.value(QStringLiteral("floatFirst"), s.floatFirst).toBool();
    s.shakeFloat = c.value(QStringLiteral("shakeFloat"), s.shakeFloat).toBool();
    if (c.contains(QStringLiteral("spawnTarget"))) {
        const QString v = c.value(QStringLiteral("spawnTarget")).toString();
        s.spawnTarget = v == QLatin1String("focused") ? SpawnTarget::Focused : v == QLatin1String("end") ? SpawnTarget::End
            : v == QLatin1String("dwindle") ? SpawnTarget::Dwindle : SpawnTarget::Pointer;
    } else if (!c.value(QStringLiteral("placeAtPointer"), true).toBool()) {
        s.spawnTarget = SpawnTarget::End; // the key this setting replaced: off meant "at the end"
    }
    s.peek = std::max(0.0, c.value(QStringLiteral("peek"), s.peek).toDouble());
    s.map = c.value(QStringLiteral("map"), s.map).toBool();
    s.mapPinned = c.value(QStringLiteral("mapPinned"), s.mapPinned).toBool();
    s.glide = c.value(QStringLiteral("glide"), s.glide).toBool();
    if (c.value(QStringLiteral("animationProfile")).toString() == QLatin1String("hyprland"))
        s.animationProfile = QStringLiteral("hyprland");
    const QString style = c.value(QStringLiteral("windowAnimation")).toString();
    if (style == QLatin1String("slide") || style == QLatin1String("gnomed")) s.windowAnimation = style;
    s.scratchpad = c.value(QStringLiteral("scratchpad"), s.scratchpad).toBool();
    s.urgent = c.value(QStringLiteral("urgent"), s.urgent).toBool();
    s.pin = c.value(QStringLiteral("pin"), s.pin).toBool();
    s.tabs = c.value(QStringLiteral("tabs"), s.tabs).toBool();
    s.landMs = std::max(0.0, c.value(QStringLiteral("landMs"), s.landMs).toDouble());
    s.tileMinWidth = std::clamp(c.value(QStringLiteral("tileMinWidth"), s.tileMinWidth).toInt(), 1, 4000);
    s.tileMinHeight = std::clamp(c.value(QStringLiteral("tileMinHeight"), s.tileMinHeight).toInt(), 1, 4000);
    s.iconBelow = std::clamp(c.value(QStringLiteral("iconBelow"), s.iconBelow).toInt(), 0, 1000);
    s.followTheme = c.value(QStringLiteral("followTheme"), s.followTheme).toBool();
    s.toasts = c.value(QStringLiteral("toasts"), s.toasts).toBool();
    s.flyTo = c.value(QStringLiteral("flyTo"), s.flyTo).toBool();
    s.whichKey = c.value(QStringLiteral("whichKey"), s.whichKey).toBool();
    s.whichKeyDelayMs = std::clamp(c.value(QStringLiteral("whichKeyDelayMs"), s.whichKeyDelayMs).toInt(), 100, 2000);
    return s;
}

bool save(const Settings &s, const QString &file)
{
    QSettings c(file, QSettings::IniFormat);
    c.setValue(QStringLiteral("closeFocus"), s.closeFocus);
    c.setValue(QStringLiteral("floatFirst"), s.floatFirst);
    c.setValue(QStringLiteral("shakeFloat"), s.shakeFloat);
    c.setValue(QStringLiteral("spawnTarget"),
               s.spawnTarget == SpawnTarget::Focused ? QStringLiteral("focused")
                   : s.spawnTarget == SpawnTarget::End ? QStringLiteral("end")
                   : s.spawnTarget == SpawnTarget::Dwindle ? QStringLiteral("dwindle") : QStringLiteral("pointer"));
    c.setValue(QStringLiteral("peek"), s.peek);
    c.setValue(QStringLiteral("map"), s.map);
    c.setValue(QStringLiteral("mapPinned"), s.mapPinned);
    c.setValue(QStringLiteral("glide"), s.glide);
    c.setValue(QStringLiteral("animationProfile"), s.animationProfile);
    c.setValue(QStringLiteral("windowAnimation"), s.windowAnimation);
    c.setValue(QStringLiteral("scratchpad"), s.scratchpad);
    c.setValue(QStringLiteral("urgent"), s.urgent);
    c.setValue(QStringLiteral("pin"), s.pin);
    c.setValue(QStringLiteral("tabs"), s.tabs);
    c.setValue(QStringLiteral("landMs"), s.landMs);
    c.setValue(QStringLiteral("tileMinWidth"), s.tileMinWidth);
    c.setValue(QStringLiteral("tileMinHeight"), s.tileMinHeight);
    c.setValue(QStringLiteral("iconBelow"), s.iconBelow);
    c.setValue(QStringLiteral("followTheme"), s.followTheme);
    c.setValue(QStringLiteral("toasts"), s.toasts);
    c.setValue(QStringLiteral("flyTo"), s.flyTo);
    c.setValue(QStringLiteral("whichKey"), s.whichKey);
    c.setValue(QStringLiteral("whichKeyDelayMs"), s.whichKeyDelayMs);
    c.sync();
    return c.status() == QSettings::NoError;
}

bool saveValue(const QString &key, const QVariant &value, const QString &file)
{
    QSettings c(file, QSettings::IniFormat);
    c.setValue(key, value);
    c.sync();
    return c.status() == QSettings::NoError;
}

QList<AppRule> loadRules(const QString &file)
{
    QSettings c(file, QSettings::IniFormat);
    c.sync();
    QList<AppRule> rules;
    c.allKeys(); // materialise the sections before asking for child groups
    c.beginGroup(QStringLiteral("ViewRules"));
    for (const QString &app : c.childGroups()) {
        c.beginGroup(app);
        AppRule r;
        r.app = app;
        r.placement = c.value(QStringLiteral("placement"), r.placement).toString();
        r.scaling = c.value(QStringLiteral("scaling"), r.scaling).toString();
        r.minWidth = c.value(QStringLiteral("minWidth"), r.minWidth).toInt();
        r.minHeight = c.value(QStringLiteral("minHeight"), r.minHeight).toInt();
        r.allowSide = c.value(QStringLiteral("allowSide"), r.allowSide).toBool();
        r.allowStage = c.value(QStringLiteral("allowStage"), r.allowStage).toBool();
        r.allowFloating = c.value(QStringLiteral("allowFloating"), r.allowFloating).toBool();
        r.singleton = c.value(QStringLiteral("singleton"), r.singleton).toBool();
        r.closable = c.value(QStringLiteral("closable"), r.closable).toBool();
        r.scratchpad = c.value(QStringLiteral("scratchpad"), r.scratchpad).toBool();
        r.floatWidth = c.value(QStringLiteral("floatWidth"), r.floatWidth).toInt();
        r.floatHeight = c.value(QStringLiteral("floatHeight"), r.floatHeight).toInt();
        r.floatAtPointer = c.value(QStringLiteral("floatAtPointer"), r.floatAtPointer).toBool();
        c.endGroup();
        rules.append(r);
    }
    return rules;
}

bool saveRules(const QList<AppRule> &rules, const QString &file)
{
    QSettings c(file, QSettings::IniFormat);
    c.remove(QStringLiteral("ViewRules"));
    c.beginGroup(QStringLiteral("ViewRules"));
    for (const auto &r : rules) {
        // "/" and "\" would split the app id into nested groups.
        QString app = r.app.trimmed();
        app.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
        if (app.isEmpty()) continue;
        c.beginGroup(app);
        c.setValue(QStringLiteral("placement"), r.placement);
        c.setValue(QStringLiteral("scaling"), r.scaling);
        c.setValue(QStringLiteral("minWidth"), r.minWidth);
        c.setValue(QStringLiteral("minHeight"), r.minHeight);
        c.setValue(QStringLiteral("allowSide"), r.allowSide);
        c.setValue(QStringLiteral("allowStage"), r.allowStage);
        c.setValue(QStringLiteral("allowFloating"), r.allowFloating);
        c.setValue(QStringLiteral("singleton"), r.singleton);
        c.setValue(QStringLiteral("closable"), r.closable);
        c.setValue(QStringLiteral("scratchpad"), r.scratchpad);
        c.setValue(QStringLiteral("floatWidth"), r.floatWidth);
        c.setValue(QStringLiteral("floatHeight"), r.floatHeight);
        c.setValue(QStringLiteral("floatAtPointer"), r.floatAtPointer);
        c.endGroup();
    }
    c.endGroup();
    c.sync();
    return c.status() == QSettings::NoError;
}

} // namespace krellis::config
