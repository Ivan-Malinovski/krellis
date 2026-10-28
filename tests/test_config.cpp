#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include "krellis_config.h"

using namespace krellis::config;

class TestConfig : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void oldTrellisFilesAreCarriedOverOnce()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
        qputenv("XDG_STATE_HOME", (home.path() + "/state").toUtf8());
        QVERIFY(QDir().mkpath(home.path() + "/config"));
        QVERIFY(QDir().mkpath(home.path() + "/state/kwin-trellis"));
        for (const QString &old : {home.path() + "/config/trellisrc", home.path() + "/state/kwin-trellis/session.json",
                                   home.path() + "/state/kwin-trellis/boot.json"}) {
            QFile f(old);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[General]\nglide=false\n");
        }
        QCOMPARE(defaultPath(), home.path() + "/config/krellisrc");
        QVERIFY(!load(defaultPath()).glide);                                   // the old setting came along
        QVERIFY(QFile::exists(home.path() + "/config/trellisrc"));             // and the old file is untouched
        QCOMPARE(stateFile("session.json"), home.path() + "/state/krellis/session.json");
        QVERIFY(QFile::exists(home.path() + "/state/krellis/session.json"));
        QVERIFY(!QFile::exists(home.path() + "/state/krellis/boot.json"));     // old crash strikes stay behind
    }
    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("krellisrc");
        Settings s;
        s.closeFocus = false; s.floatFirst = true; s.shakeFloat = false; s.spawnTarget = SpawnTarget::Focused; s.peek = 12; s.map = false; s.mapPinned = true; s.glide = true; s.landMs = 90; s.iconBelow = 40; s.tileMinWidth = 320; s.tileMinHeight = 200; s.scratchpad = true; s.pin = true; s.tabs = false;
        s.followTheme = false; s.toasts = false; s.flyTo = true; s.whichKey = false; s.whichKeyDelayMs = 750;
        QVERIFY(save(s, file));
        QCOMPARE(load(file), s);
        QVERIFY(!Settings{}.scratchpad);
        QVERIFY(!Settings{}.pin);
        QVERIFY(Settings{}.tabs);
        QVERIFY(Settings{}.map);
    }
    void polishSettingsDefaultsAndClamps()
    {
        const Settings d;
        QVERIFY(d.followTheme && d.toasts && d.whichKey && d.shakeFloat);
        QVERIFY(!d.flyTo);
        QCOMPARE(d.whichKeyDelayMs, 400);
        QTemporaryDir dir;
        const QString file = dir.filePath("krellisrc");
        QVERIFY(saveValue("whichKeyDelayMs", 5, file));
        QCOMPARE(load(file).whichKeyDelayMs, 100);
        QVERIFY(saveValue("whichKeyDelayMs", 99999, file));
        QCOMPARE(load(file).whichKeyDelayMs, 2000);
        QVERIFY(saveValue("followTheme", false, file));
        QVERIFY(!load(file).followTheme);
    }
    void spawnTargetValuesAndLegacyKey()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("krellisrc");
        for (auto t : {SpawnTarget::Pointer, SpawnTarget::Focused, SpawnTarget::End}) {
            Settings s;
            s.spawnTarget = t;
            QVERIFY(save(s, file));
            QCOMPARE(load(file).spawnTarget, t);
        }
        // The file of an older version: placeAtPointer=false meant "at the end", true the default.
        const QString old = dir.filePath("old");
        QVERIFY(saveValue("placeAtPointer", false, old));
        QCOMPARE(load(old).spawnTarget, SpawnTarget::End);
        QVERIFY(saveValue("placeAtPointer", true, old));
        QCOMPARE(load(old).spawnTarget, SpawnTarget::Pointer);
        // The new key wins over a stale old one, and a value we do not know falls back to the default.
        QVERIFY(saveValue("placeAtPointer", false, old));
        QVERIFY(saveValue("spawnTarget", "focused", old));
        QCOMPARE(load(old).spawnTarget, SpawnTarget::Focused);
        QVERIFY(saveValue("spawnTarget", "sideways", old));
        QCOMPARE(load(old).spawnTarget, SpawnTarget::Pointer);
        QVERIFY(Settings{}.spawnTarget == SpawnTarget::Pointer);
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
        const QString file = dir.filePath("krellisrc");
        QVERIFY(saveValue("floatFirst", true, file));
        QVERIFY(load(file).floatFirst);
        QVERIFY(saveValue("mapPinned", true, file));
        QVERIFY(load(file).mapPinned);
        QVERIFY(load(file).floatFirst);
    }
    void saveKeepsRulesAndUnknownKeys()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("krellisrc");
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
        const QString file = dir.filePath("krellisrc");
        AppRule a; a.app = "org.kde.dolphin"; a.placement = "floating"; a.scaling = "inert"; a.minWidth = 300; a.minHeight = 200;
        a.allowStage = false; a.singleton = true; a.closable = false; a.scratchpad = true;
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
        const QString file = dir.filePath("krellisrc");
        AppRule r; r.app = "krellis-stage"; r.placement = "stage";
        QVERIFY(saveRules({r}, file));
        QFile f(file); QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY2(text.contains("[ViewRules]"), text.constData());
        QVERIFY2(text.contains("krellis-stage\\placement=stage"), text.constData());
    }
    void appIdWithSeparatorsStaysOneGroup()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("krellisrc");
        AppRule r; r.app = "a/b\\c"; 
        QVERIFY(saveRules({r}, file));
        const auto back = loadRules(file);
        QCOMPARE(back.size(), 1);
        QCOMPARE(back[0].app, QString("a_b_c"));
    }
};

QTEST_APPLESS_MAIN(TestConfig)
#include "test_config.moc"
