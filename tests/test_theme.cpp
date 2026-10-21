#include "theme_tokens.h"

#include <QtTest>

using namespace trellis;

class TestTheme : public QObject
{
    Q_OBJECT
private slots:
    void literalIsTheOldPalette()
    {
        const auto t = literalTokens();
        QCOMPARE(t.surface.name(), QStringLiteral("#1a1d26"));
        QCOMPARE(t.accent.name(), QStringLiteral("#99b4ff"));
        QCOMPARE(t.text.name(), QStringLiteral("#e6e9f2"));
        QVERIFY(t.isDark);
    }
    void darkSchemeGivesLightTextOnDarkSurface()
    {
        const auto t = deriveTokens({QColor("#232629"), QColor("#fcfcfc"), QColor("#3daee9"), QColor("#da4453")});
        QVERIFY(t.isDark);
        QVERIFY(t.text.lightnessF() > t.surface.lightnessF());
        QVERIFY(contrast(t.text, t.surface) >= 7.0);
        QVERIFY(contrast(t.textDim, t.surface) >= 3.0);
        QVERIFY(contrast(t.accent, t.surface) >= 3.0);
        QCOMPARE(t.error.name(), QStringLiteral("#da4453"));
    }
    void lightSchemeGivesDarkTextOnLightSurface()
    {
        const auto t = deriveTokens({QColor("#eff0f1"), QColor("#232629"), QColor("#3daee9"), QColor()});
        QVERIFY(!t.isDark);
        QVERIFY(t.text.lightnessF() < t.surface.lightnessF());
        QVERIFY(contrast(t.text, t.surface) >= 7.0);
        QVERIFY(contrast(t.textDim, t.surface) >= 3.0);
        QVERIFY(contrast(t.accent, t.surface) >= 3.0); // a pale accent is darkened on a light surface
        QCOMPARE(t.wash, t.text);                      // hairlines are dark on light
        QCOMPARE(t.error, literalTokens().error);      // no negative colour given
    }
    void aCustomAccentPassesThroughWhenItAlreadyContrasts()
    {
        const auto t = deriveTokens({QColor("#1b1e20"), QColor("#eff0f1"), QColor("#ff6a00"), QColor()});
        QCOMPARE(t.accent.name(), QStringLiteral("#ff6a00"));
    }
    void aLowContrastAccentIsPulledAwayFromTheSurface()
    {
        const auto t = deriveTokens({QColor("#232629"), QColor("#fcfcfc"), QColor("#26292d"), QColor()});
        QVERIFY(contrast(t.accent, t.surface) >= 3.0);
    }
    void panelsStayReadableWhenTranslucent()
    {
        // Surfaces are drawn at 90% over whatever is behind. Worst case behind a dark scheme is a white window.
        for (const SchemeInputs &in : {SchemeInputs{QColor("#232629"), QColor("#fcfcfc"), QColor("#3daee9"), {}},
                                       SchemeInputs{QColor("#eff0f1"), QColor("#232629"), QColor("#3daee9"), {}}}) {
            const auto t = deriveTokens(in);
            for (const QColor &behind : {QColor(Qt::white), QColor(Qt::black)})
                QVERIFY(contrast(t.text, mix(behind, t.surface, 0.9)) >= 4.5);
        }
    }
};

QTEST_APPLESS_MAIN(TestTheme)
#include "test_theme.moc"
