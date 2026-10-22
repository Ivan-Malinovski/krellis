#include "fuzzy.h"

#include <QtTest>

using namespace krellis;

namespace {
std::optional<int> score(const std::string &q, const std::string &title, const std::string &app = {})
{
    std::vector<FuzzyField> fields{{title, 1.0}};
    if (!app.empty()) fields.push_back({app, 0.8});
    return fuzzyScore(q, fields);
}
}

class TestFuzzy : public QObject
{
    Q_OBJECT
private slots:
    void anEmptyQueryMatchesEverythingAndKeepsTheOrder()
    {
        QCOMPARE(score("", "anything"), std::optional<int>(0));
        QCOMPARE(score("   ", "anything"), std::optional<int>(0));
        const auto order = fuzzyRank("", {{{"b", 1}}, {{"a", 1}}, {{"c", 1}}});
        QCOMPARE(order, (std::vector<size_t>{0, 1, 2}));
    }
    void lettersMustAppearInOrder()
    {
        QVERIFY(score("fx", "Firefox"));
        QVERIFY(!score("xf", "Firefox"));
        QVERIFY(!score("firefoxx", "Firefox"));
        QVERIFY(!score("zzz", "Firefox"));
    }
    void caseDoesNotMatter()
    {
        QVERIFY(score("FIRE", "firefox"));
        QVERIFY(score("fire", "FIREFOX"));
        QVERIFY(score("ørest", "Øresund notes")); // Latin-1 folds
        QVERIFY(score("софия", "СОФИЯ - карта")); // Cyrillic folds
        QVERIFY(score("καλ", "ΚΑΛΗΜΕΡΑ"));       // Greek folds
    }
    void aWordStartBeatsALetterInTheMiddle()
    {
        QVERIFY(*score("ter", "Terminal") > *score("ter", "Alacritty - cluster"));
        QVERIFY(*score("nt", "New Tab") > *score("nt", "Mount"));
    }
    void consecutiveLettersBeatScatteredOnes()
    {
        QVERIFY(*score("code", "VS Code") > *score("code", "xcxoxdxe"));
    }
    void aPrefixBeatsALateMatch()
    {
        QVERIFY(*score("kate", "Kate") > *score("kate", "Notes - Kate"));
    }
    void everyWordMustMatchButNotInOrder()
    {
        QVERIFY(score("tab fire", "New Tab - Firefox"));
        QVERIFY(!score("tab chrome", "New Tab - Firefox"));
    }
    void aWordCanMatchTheApplicationInsteadOfTheTitle()
    {
        QVERIFY(!score("fire", "GitHub - Pull requests"));
        QVERIFY(score("fire", "GitHub - Pull requests", "org.mozilla.firefox"));
        QVERIFY(score("pull fire", "GitHub - Pull requests", "org.mozilla.firefox"));
    }
    void aTitleMatchOutranksAnApplicationMatch()
    {
        QVERIFY(*score("term", "Terminal", "other") > *score("term", "other", "terminal"));
    }
    void rankingIsBestFirstAndStableAmongEquals()
    {
        const std::vector<std::vector<FuzzyField>> c{
            {{"Mount", 1}}, {{"Terminal", 1}}, {{"Terminal two", 1}}, {{"nothing", 1}}};
        const auto order = fuzzyRank("ter", c);
        QCOMPARE(order.size(), size_t(2));
        QCOMPARE(order[0], size_t(1)); // equal scores keep the caller's order
        QCOMPARE(order[1], size_t(2));
    }
    void invalidUtf8DoesNotThrowOrMatchWrongly()
    {
        QVERIFY(!score("a", std::string("\xC3")));
        QVERIFY(score("a", std::string("\xFF" "a")));
    }
    void aLongTitleStaysCheap()
    {
        const std::string title(4000, 'a');
        QVERIFY(score("aaa", title));
        QVERIFY(!score("b", title));
    }
};

QTEST_APPLESS_MAIN(TestFuzzy)
#include "test_fuzzy.moc"
