#include "theme.h"

#include <KColorScheme>
#include <KConfigGroup>
#include <KSharedConfig>

namespace trellis {

Theme::Theme(QObject *parent)
    : QObject(parent)
{
}

void Theme::setFollow(bool follow)
{
    if (follow == m_follow)
        return;
    m_follow = follow;
    if (follow && !m_watcher) {
        m_watcher = KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kdeglobals")));
        connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group, const QByteArrayList &) {
            const QString name = group.name();
            if (name == QLatin1String("General") || name.startsWith(QLatin1String("Colors:")))
                reload();
        });
    }
    if (follow)
        KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->reparseConfiguration();
    refresh();
}

void Theme::reload()
{
    if (!m_follow)
        return;
    KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->reparseConfiguration();
    refresh();
}

void Theme::refresh()
{
    const ThemeTokens next = compute();
    if (next == m_tokens)
        return;
    m_tokens = next;
    Q_EMIT changed();
}

ThemeTokens Theme::compute() const
{
    if (!m_follow)
        return literalTokens();
    const auto config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    // Without a colour scheme in kdeglobals KColorScheme answers with its built-in light colours, which would
    // make a bare session look different from the built-in look for no reason.
    if (!config->hasGroup(QStringLiteral("Colors:Window")))
        return literalTokens();
    const KColorScheme window(QPalette::Active, KColorScheme::Window, config);
    const KColorScheme selection(QPalette::Active, KColorScheme::Selection, config);
    SchemeInputs in;
    in.window = window.background().color();
    in.windowText = window.foreground().color();
    in.negative = window.foreground(KColorScheme::NegativeText).color();
    in.accent = selection.background().color();
    const QStringList accent = KConfigGroup(config, QStringLiteral("General")).readEntry("AccentColor", QStringList());
    if (accent.size() == 3) {
        const QColor c(accent[0].toInt(), accent[1].toInt(), accent[2].toInt());
        if (c.isValid())
            in.accent = c;
    }
    return deriveTokens(in);
}

QString Theme::dump() const
{
    const auto &t = m_tokens;
    QString out = QStringLiteral("THEME follow=%1 dark=%2").arg(m_follow).arg(t.isDark);
    const auto add = [&](const char *name, const QColor &c) { out += QStringLiteral(" %1=%2").arg(QLatin1String(name), c.name()); };
    add("surface", t.surface);
    add("text", t.text);
    add("textDim", t.textDim);
    add("accent", t.accent);
    add("accentStrong", t.accentStrong);
    add("card", t.card);
    add("well", t.well);
    add("error", t.error);
    return out;
}

} // namespace trellis
