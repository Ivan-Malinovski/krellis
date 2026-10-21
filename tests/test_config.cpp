#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include "trellis_config.h"

using namespace trellis::config;

class TestConfig : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("trellisrc");
        Settings s;
        s.floatFirst = true; s.placeAtPointer = false; s.peek = 12; s.mapPinned = true; s.glide = true; s.landMs = 90;
        QVERIFY(save(s, file));
        QCOMPARE(load(file), s);
    }
    void defaultsWhenMissing()
    {
        QTemporaryDir dir;
        QCOMPARE(load(dir.filePath("nothing")), Settings{});
    }
    void liveToggleIsReadBack()
    {
        // A live toggle must land where load() looks; it used to go to [%General] and never came back.
        QTemporaryDir dir;
        const QString file = dir.filePath("trellisrc");
        QVERIFY(saveValue("floatFirst", true, file));
        QVERIFY(load(file).floatFirst);
        QVERIFY(saveValue("mapPinned", true, file));
        QVERIFY(load(file).mapPinned);
        QVERIFY(load(file).floatFirst);
    }
    void saveKeepsRulesAndUnknownKeys()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("trellisrc");
        AppRule r; r.app = "konsole"; r.placement = "stage";
        QVERIFY(saveRules({r}, file));
        { QSettings c(file, QSettings::IniFormat); c.setValue("future", 7); }
        QVERIFY(save(Settings{}, file));
        QCOMPARE(loadRules(file), QList<AppRule>{r});
        QCOMPARE(QSettings(file, QSettings::IniFormat).value("future").toInt(), 7);
    }
    void rulesRoundTripAndReplace()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("trellisrc");
        AppRule a; a.app = "org.kde.dolphin"; a.placement = "floating"; a.scaling = "inert"; a.minWidth = 300; a.minHeight = 200;
        a.allowStage = false; a.singleton = true; a.closable = false;
        AppRule b; b.app = "mpv";
        QVERIFY(saveRules({a, b}, file));
        QCOMPARE(loadRules(file), (QList<AppRule>{b, a})); // groups come back sorted
        QVERIFY(saveRules({b}, file));
        QCOMPARE(loadRules(file), QList<AppRule>{b});
        QVERIFY(saveRules({}, file));
        QVERIFY(loadRules(file).isEmpty());
    }
    void rulesUseTheLoadersSyntax()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("trellisrc");
        AppRule r; r.app = "trellis-stage"; r.placement = "stage";
        QVERIFY(saveRules({r}, file));
        QFile f(file); QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY2(text.contains("[ViewRules]"), text.constData());
        QVERIFY2(text.contains("trellis-stage\\placement=stage"), text.constData());
    }
    void appIdWithSeparatorsStaysOneGroup()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("trellisrc");
        AppRule r; r.app = "a/b\\c"; 
        QVERIFY(saveRules({r}, file));
        const auto back = loadRules(file);
        QCOMPARE(back.size(), 1);
        QCOMPARE(back[0].app, QString("a_b_c"));
    }
};

QTEST_APPLESS_MAIN(TestConfig)
#include "test_config.moc"
