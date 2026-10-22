#pragma once

#include "theme_tokens.h"

#include <QObject>

#include <KConfigWatcher>

namespace krellis {

// The tokens the QML overlays bind to (`required property QtObject theme`). With `follow` off they are the built-in
// palette; on, they come from the Plasma colour scheme and accent in kdeglobals and track changes live.
class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY changed)
    Q_PROPERTY(QColor card READ card NOTIFY changed)
    Q_PROPERTY(QColor cardBorder READ cardBorder NOTIFY changed)
    Q_PROPERTY(QColor well READ well NOTIFY changed)
    Q_PROPERTY(QColor wellHover READ wellHover NOTIFY changed)
    Q_PROPERTY(QColor stage READ stage NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textSoft READ textSoft NOTIFY changed)
    Q_PROPERTY(QColor textDim READ textDim NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentStrong READ accentStrong NOTIFY changed)
    Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY changed)
    Q_PROPERTY(QColor wash READ wash NOTIFY changed)
    Q_PROPERTY(QColor halo READ halo NOTIFY changed)
    Q_PROPERTY(QColor tileOutline READ tileOutline NOTIFY changed)
    Q_PROPERTY(QColor warm READ warm NOTIFY changed)
    Q_PROPERTY(QColor cool READ cool NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    Q_PROPERTY(bool isDark READ isDark NOTIFY changed)
public:
    explicit Theme(QObject *parent = nullptr);

    void setFollow(bool follow);
    bool following() const { return m_follow; }
    /// Re-read kdeglobals (a no-op while not following).
    void reload();
    const ThemeTokens &tokens() const { return m_tokens; }
    /// One line per token, for the nested harness.
    QString dump() const;

    QColor surface() const { return m_tokens.surface; }
    QColor surfaceHover() const { return m_tokens.surfaceHover; }
    QColor card() const { return m_tokens.card; }
    QColor cardBorder() const { return m_tokens.cardBorder; }
    QColor well() const { return m_tokens.well; }
    QColor wellHover() const { return m_tokens.wellHover; }
    QColor stage() const { return m_tokens.stage; }
    QColor text() const { return m_tokens.text; }
    QColor textSoft() const { return m_tokens.textSoft; }
    QColor textDim() const { return m_tokens.textDim; }
    QColor accent() const { return m_tokens.accent; }
    QColor accentStrong() const { return m_tokens.accentStrong; }
    QColor accentSoft() const { return m_tokens.accentSoft; }
    QColor wash() const { return m_tokens.wash; }
    QColor halo() const { return m_tokens.halo; }
    QColor tileOutline() const { return m_tokens.tileOutline; }
    QColor warm() const { return m_tokens.warm; }
    QColor cool() const { return m_tokens.cool; }
    QColor error() const { return m_tokens.error; }
    bool isDark() const { return m_tokens.isDark; }

Q_SIGNALS:
    void changed();

private:
    ThemeTokens compute() const;
    void refresh();

    bool m_follow = false;
    ThemeTokens m_tokens = literalTokens();
    KConfigWatcher::Ptr m_watcher;
};

} // namespace krellis
