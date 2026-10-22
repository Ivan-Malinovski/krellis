// The colours the overlays are drawn with. Pure (QColor only), so the derivation can be tested without KDE.
#pragma once

#include <QColor>

#include <algorithm>
#include <cmath>

namespace krellis {

struct ThemeTokens {
    QColor surface;       // panels and cards; QML applies the translucency
    QColor surfaceHover;  // a hovered tab
    QColor card;          // a tile's stand-in card (drawer tabs, tile pieces)
    QColor cardBorder;
    QColor well;          // buttons in the bar
    QColor wellHover;
    QColor stage;         // the minimap's backdrop
    QColor text;
    QColor textSoft;
    QColor textDim;
    QColor accent;        // the camera frame, pinned and open states
    QColor accentStrong;  // a filled current item
    QColor accentSoft;    // the rename field and the scratch layer
    QColor wash;          // hairlines and hover washes: this colour at low alpha
    QColor halo;          // behind the camera frame, so it reads on any tile
    QColor tileOutline;   // the focused tile's border
    QColor warm;          // floating mode
    QColor cool;          // tiling mode
    QColor error;
    bool isDark = true;
    bool operator==(const ThemeTokens &) const = default;
};

/// What a Plasma colour scheme provides.
struct SchemeInputs {
    QColor window, windowText, accent, negative;
};

/// The built-in look.
inline ThemeTokens literalTokens()
{
    ThemeTokens t;
    t.surface = QColor(0x1a, 0x1d, 0x26);
    t.surfaceHover = QColor(0x2c, 0x31, 0x42);
    t.card = QColor(0x24, 0x2b, 0x3b);
    t.cardBorder = QColor(0x56, 0x62, 0x7f);
    t.well = QColor(0x3a, 0x40, 0x54);
    t.wellHover = QColor(0x4b, 0x52, 0x70);
    t.stage = QColor(0x2a, 0x2f, 0x3d);
    t.text = QColor(0xe6, 0xe9, 0xf2);
    t.textSoft = QColor(0xc9, 0xce, 0xe0);
    t.textDim = QColor(0x8c, 0x93, 0xa8);
    t.accent = QColor(0x99, 0xb4, 0xff);
    t.accentStrong = QColor(0x4a, 0x5f, 0xb4);
    t.accentSoft = QColor(0x84, 0x96, 0xec);
    t.wash = QColor(0xff, 0xff, 0xff);
    t.halo = QColor(0x10, 0x12, 0x18);
    t.tileOutline = QColor(0xff, 0xff, 0xff);
    t.warm = QColor(0xd8, 0xab, 0x35);
    t.cool = QColor(0x3f, 0x8f, 0xd6);
    t.error = QColor(0xeb, 0x8b, 0x9c);
    t.isDark = true;
    return t;
}

inline QColor mix(const QColor &a, const QColor &b, double toB)
{
    const auto m = [&](double x, double y) { return x + (y - x) * toB; };
    return QColor::fromRgbF(m(a.redF(), b.redF()), m(a.greenF(), b.greenF()), m(a.blueF(), b.blueF()));
}

/// WCAG contrast ratio of two opaque colours.
inline double contrast(const QColor &a, const QColor &b)
{
    const auto lum = [](const QColor &c) {
        const auto f = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * f(c.redF()) + 0.7152 * f(c.greenF()) + 0.0722 * f(c.blueF());
    };
    const double la = lum(a), lb = lum(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// Tokens from a scheme: surfaces and text from Window and WindowText, the accent as given. A dark scheme is
/// told apart by its window colour being darker than its text.
inline ThemeTokens deriveTokens(const SchemeInputs &s)
{
    ThemeTokens t;
    const QColor bg = QColor::fromRgb(s.window.rgb()), fg = QColor::fromRgb(s.windowText.rgb());
    t.isDark = bg.lightnessF() < fg.lightnessF();
    t.surface = bg;
    t.surfaceHover = mix(bg, fg, 0.16);
    t.card = mix(bg, fg, 0.08);
    t.cardBorder = mix(bg, fg, 0.30);
    t.well = mix(bg, fg, 0.16);
    t.wellHover = mix(bg, fg, 0.26);
    t.stage = mix(bg, fg, 0.10);
    t.text = fg;
    t.textSoft = mix(fg, bg, 0.12);
    t.textDim = mix(fg, bg, 0.42);
    // The accent has to stand out on the surface: pull it toward the text colour until it does.
    QColor accent = QColor::fromRgb(s.accent.rgb());
    for (int i = 0; i < 10 && contrast(accent, bg) < 3.0; ++i)
        accent = mix(accent, fg, 0.12);
    t.accent = accent;
    t.accentStrong = mix(accent, bg, t.isDark ? 0.45 : 0.15);
    t.accentSoft = accent;
    t.wash = fg;
    t.halo = t.isDark ? QColor(0x08, 0x09, 0x0c) : QColor(0xff, 0xff, 0xff);
    t.tileOutline = fg;
    t.warm = t.isDark ? QColor(0xd8, 0xab, 0x35) : QColor(0xa8, 0x77, 0x00);
    t.cool = t.isDark ? QColor(0x3f, 0x8f, 0xd6) : QColor(0x1f, 0x6f, 0xb6);
    t.error = s.negative.isValid() ? QColor::fromRgb(s.negative.rgb()) : QColor(0xeb, 0x8b, 0x9c);
    return t;
}

} // namespace krellis
