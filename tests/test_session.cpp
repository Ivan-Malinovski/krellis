#include "session.h"
#include "session_store.h"
#include "spatial.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <limits>

using namespace trellis;

namespace {
using Strings = std::vector<std::string>;
using Matches = std::map<size_t, size_t>;

NodePtr tile(const std::string &id) { return makePanel(id, {id + "-view"}); }

void addRecords(SavedCanvas &canvas)
{
    for (const auto &panel : allPanels(canvas.document)) {
        canvas.windows[panel->id] = {"org.example." + panel->id, "main"};
        for (const auto &view : panel->views)
            canvas.document.views[view] = {"native", "PRIVATE_CAPTION_TOKEN", {{"pid", "PRIVATE_PID_TOKEN"}}};
    }
}

SessionSnapshot sample()
{
    SavedCanvas primary;
    primary.output = "monitor:primary";
    auto &doc = primary.document;
    doc.root = makeSplit("layout", Axis::X, {tile("editor"), makeStage("desktop", tile("terminal"))}, {0.35, 0.65});
    doc.floating.push_back({tile("browser"), {0.08, 0.15, 0.42, 0.3}, 4, FloatingLayer::Overlay});

    RestoreTarget dock;
    dock.kind = RestoreTarget::Kind::Docked;
    dock.beside = "editor";
    dock.edge = Edge::Bottom;
    dock.share = 0.3;
    doc.hidden.push_back({tile("notes"), dock});
    RestoreTarget floating;
    floating.kind = RestoreTarget::Kind::Floating;
    floating.rect = {0.22, 0.16, 0.31, 0.27};
    floating.layer = FloatingLayer::Stage;
    doc.hidden.push_back({tile("tools"), floating});
    RestoreTarget tab;
    tab.kind = RestoreTarget::Kind::Tab;
    tab.panel = "desktop";
    doc.hidden.push_back({tile("search"), tab});
    doc.navigation.frame = {"editor", "terminal"};
    doc.navigation.framings = {{"view-work", "Work", {"editor", "terminal"}},
                              {"view-stage", "Desktop", {"desktop"}},
                              {"view-old", "Closed", {"missing"}}};
    addRecords(primary);

    SavedCanvas secondary;
    secondary.output = "connector:secondary";
    secondary.document.root = tile("music");
    addRecords(secondary);
    return {primary, secondary};
}

std::map<std::string, NodePtr> bindings(const LayoutDocument &doc)
{
    std::map<std::string, NodePtr> result;
    for (const auto &panel : allPanels(doc))
        result[panel->id] = makePanel("live-" + panel->id, {"current-" + panel->id});
    return result;
}

QByteArray editRoot(const std::function<void(QJsonObject &)> &edit)
{
    auto root = QJsonDocument::fromJson(encodeSession(sample())).object();
    edit(root);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray editCanvas(const std::function<void(QJsonObject &)> &edit)
{
    return editRoot([&](QJsonObject &root) {
        auto canvases = root["canvases"].toArray();
        auto canvas = canvases[0].toObject();
        edit(canvas);
        canvases[0] = canvas;
        root["canvases"] = canvases;
    });
}

QByteArray editSplit(const std::function<void(QJsonObject &)> &edit)
{
    return editCanvas([&](QJsonObject &canvas) {
        auto split = canvas["root"].toObject();
        edit(split);
        canvas["root"] = split;
    });
}
}

class TestSession : public QObject
{
    Q_OBJECT
private slots:
    void uniqueMatchesRequireSingletonOnBothSides_data()
    {
        QTest::addColumn<int>("savedCount");
        QTest::addColumn<int>("liveCount");
        QTest::addColumn<bool>("matches");
        QTest::newRow("one-to-one") << 1 << 1 << true;
        QTest::newRow("saved-duplicate") << 2 << 1 << false;
        QTest::newRow("live-duplicate") << 1 << 2 << false;
        QTest::newRow("duplicates-on-both-sides") << 2 << 2 << false;
        QTest::newRow("no-arrival") << 1 << 0 << false;
        QTest::newRow("no-saved-slot") << 0 << 1 << false;
    }

    void uniqueMatchesRequireSingletonOnBothSides()
    {
        QFETCH(int, savedCount);
        QFETCH(int, liveCount);
        QFETCH(bool, matches);
        const WindowIdentity key{"org.example.editor", "main"};
        const auto result = uniqueWindowMatches(std::vector<WindowIdentity>(savedCount, key),
                                                std::vector<WindowIdentity>(liveCount, key));
        QCOMPARE(result.size(), matches ? size_t(1) : size_t(0));
        if (matches)
            QVERIFY(result == (Matches{{0, 0}}));
    }

    void rolesDiscriminateWindowsWithoutWildcardMatching()
    {
        const std::vector<WindowIdentity> saved{{"app", "compose"}, {"app", "inbox"}, {"app", ""}, {"other", "inbox"}};
        const std::vector<WindowIdentity> live{{"app", "inbox"}, {"other", "inbox"}, {"app", "compose"}, {"app", "other"}};
        QVERIFY(uniqueWindowMatches(saved, live) == (Matches{{0, 2}, {1, 0}, {3, 1}}));
        QVERIFY(uniqueWindowMatches({{"app", ""}}, {{"app", "named"}}).empty());
    }

    void emptyAppIdentityNeverMatchesEvenWithRole()
    {
        const std::vector<WindowIdentity> saved{{"", "main"}, {"", ""}, {"valid", ""}};
        QVERIFY(uniqueWindowMatches(saved, saved) == (Matches{{2, 2}}));
    }

    void arrivalOrderAndUnrelatedAmbiguityDoNotChangeUniqueMatches()
    {
        const std::vector<WindowIdentity> saved{{"mail", ""}, {"editor", ""}, {"ambiguous", ""}, {"ambiguous", ""}};
        const std::vector<WindowIdentity> live{{"unrelated", ""}, {"editor", ""}, {"ambiguous", ""}, {"mail", ""}};
        QVERIFY(uniqueWindowMatches(saved, live) == (Matches{{0, 3}, {1, 1}}));
        QVERIFY(uniqueWindowMatches(saved, {{"mail", ""}}) == (Matches{{0, 0}}));
        QVERIFY(uniqueWindowMatches(saved, {{"editor", ""}, {"mail", ""}}) == (Matches{{0, 1}, {1, 0}}));
    }

    void rebindPreservesRatiosFloatingGeometryAndEveryRestoreKind()
    {
        const auto saved = sample().front().document;
        const auto replacement = bindings(saved);
        const auto rebound = rebindSession(saved, replacement);
        QVERIFY(rebound.root);
        QCOMPARE(rebound.root->id, std::string("layout"));
        QVERIFY(rebound.root->axis == Axis::X);
        QCOMPARE(rebound.root->weights, saved.root->weights);
        QCOMPARE(leafIds(rebound.root), (Strings{"live-editor", "live-terminal"}));
        QCOMPARE(findStage(rebound.root)->id, std::string("desktop"));
        QCOMPARE(rebound.floating.size(), size_t(1));
        QCOMPARE(rebound.floating.front().panel->id, replacement.at("browser")->id);
        QCOMPARE(rebound.floating.front().panel->views, replacement.at("browser")->views);
        QVERIFY(sameRect(rebound.floating.front().rect, saved.floating.front().rect));
        QCOMPARE(rebound.floating.front().z, 4);
        QVERIFY(rebound.floating.front().layer == FloatingLayer::Overlay);

        QCOMPARE(rebound.hidden.size(), size_t(3));
        const auto dock = locatePanel(rebound, "live-notes");
        QVERIFY(dock.where == PanelLocation::Where::Hidden);
        QVERIFY(dock.restore.kind == RestoreTarget::Kind::Docked);
        QCOMPARE(dock.restore.beside, std::string("live-editor"));
        QVERIFY(dock.restore.edge == Edge::Bottom);
        QCOMPARE(dock.restore.share, 0.3);
        const auto floating = locatePanel(rebound, "live-tools").restore;
        QVERIFY(floating.kind == RestoreTarget::Kind::Floating);
        QVERIFY(floating.layer == FloatingLayer::Stage);
        QVERIFY(sameRect(floating.rect, saved.hidden[1].restore.rect));
        const auto tab = locatePanel(rebound, "live-search").restore;
        QVERIFY(tab.kind == RestoreTarget::Kind::Tab);
        QCOMPARE(tab.panel, std::string("desktop"));
        QCOMPARE(rebound.views.size(), size_t(6));
        QVERIFY(!rebound.views.count("editor-view"));
        QVERIFY(rebound.views.count("current-editor"));
        QCOMPARE(saved.root->children.front()->id, std::string("editor"));
    }

    void rebindMapsFrameMembersAndKeepsSavedFramingIds()
    {
        auto saved = sample().front().document;
        saved.navigation.frame = {"editor", "missing", "desktop"};
        const auto rebound = rebindSession(saved, bindings(saved));
        QCOMPARE(rebound.navigation.frame, (Strings{"live-editor", "desktop"}));
        QCOMPARE(rebound.navigation.framings.size(), size_t(2));
        const auto &work = rebound.navigation.framings[0];
        QCOMPARE(work.id, std::string("view-work"));
        QCOMPARE(work.name, std::string("Work"));
        QCOMPARE(work.frame, (Strings{"live-editor", "live-terminal"}));
        QCOMPARE(rebound.navigation.framings[1].id, std::string("view-stage"));
        QCOMPARE(rebound.navigation.framings[1].frame, (Strings{"desktop"}));
    }

    void rebindPrunesUnmatchedPanelsAndRenormalizesSurvivingRatios()
    {
        LayoutDocument saved;
        saved.root = makeSplit("row", Axis::X, {tile("one"), tile("two"), tile("three")}, {0.2, 0.3, 0.5});
        saved.floating.push_back({tile("absent-float"), {0.1, 0.1, 0.2, 0.2}, 1, FloatingLayer::Overlay});
        RestoreTarget restore;
        restore.kind = RestoreTarget::Kind::Docked;
        restore.beside = "two";
        saved.hidden.push_back({tile("hidden"), restore});
        saved.hidden.push_back({tile("absent-hidden"), restore});
        saved.navigation.frame = {"one", "two"};
        saved.navigation.framings = {{"lost", "Lost", {"two"}}, {"partial", "Partial", {"two", "three"}}};
        const auto rebound = rebindSession(saved, {{"one", tile("new-one")}, {"three", tile("new-three")},
                                                   {"hidden", tile("new-hidden")}});
        QCOMPARE(leafIds(rebound.root), (Strings{"new-one", "new-three"}));
        QVERIFY(qAbs(rebound.root->weights[0] - 2.0 / 7) < 1e-12);
        QVERIFY(qAbs(rebound.root->weights[1] - 5.0 / 7) < 1e-12);
        QVERIFY(rebound.floating.empty());
        QCOMPARE(rebound.hidden.size(), size_t(1));
        QVERIFY(rebound.hidden.front().restore.beside.empty());
        QCOMPARE(rebound.navigation.frame, (Strings{"new-one"}));
        QCOMPARE(rebound.navigation.framings.size(), size_t(1));
        QCOMPARE(rebound.navigation.framings.front().id, std::string("partial"));
        QCOMPARE(rebound.navigation.framings.front().frame, (Strings{"new-three"}));
        const auto empty = rebindSession(saved, {});
        QVERIFY(!empty.root);
        QVERIFY(empty.floating.empty());
        QVERIFY(empty.hidden.empty());
        QVERIFY(empty.views.empty());
        QVERIFY(empty.navigation.framings.empty());
    }

    void rebindRetainsSurvivingSplitAsHiddenRestoreAnchor()
    {
        LayoutDocument saved;
        saved.root = makeSplit("outer", Axis::X,
                               {makeSplit("group", Axis::Y, {tile("one"), tile("two")}, {0.25, 0.75}), tile("three")},
                               {0.6, 0.4});
        saved = hidePanel(saved, "three");
        QCOMPARE(saved.hidden.front().restore.beside, std::string("group"));
        const auto rebound = rebindSession(saved, bindings(saved));
        QVERIFY(findNode(rebound.root, "group"));
        QCOMPARE(rebound.hidden.front().restore.beside, std::string("group"));
        const auto restored = restorePanel(rebound, "live-three", FloatingLayer::Overlay);
        QVERIFY(locatePanel(restored, "live-three").where == PanelLocation::Where::Docked);
        QVERIFY(restored.floating.empty());
    }

    void rebindRenamesStructuresThatCollideWithLivePanelIds_data()
    {
        QTest::addColumn<bool>("stage");
        QTest::newRow("split-anchor") << false;
        QTest::newRow("stage-anchor") << true;
    }

    void rebindRenamesStructuresThatCollideWithLivePanelIds()
    {
        QFETCH(bool, stage);
        LayoutDocument saved;
        const auto group = makeSplit(stage ? "group" : "p1", Axis::Y, {tile("old-a"), tile("old-b")}, {0.4, 0.6});
        saved.root = stage ? makeStage("p1", group) : group;
        RestoreTarget restore;
        restore.kind = stage ? RestoreTarget::Kind::Tab : RestoreTarget::Kind::Docked;
        if (stage)
            restore.panel = "p1";
        else
            restore.beside = "p1";
        saved.hidden.push_back({tile("old-hidden"), restore});
        saved.navigation.frame = {"p1", "old-a"};
        saved.navigation.framings = {{"bookmark", "Group", {"p1"}}};

        const auto rebound = rebindSession(saved, {{"old-a", tile("p1")}, {"old-b", tile("p2")},
                                                   {"old-hidden", tile("p3")}});
        QVERIFY(rebound.root);
        const auto structuralId = rebound.root->id;
        QVERIFY(structuralId != "p1");
        QVERIFY(findNode(rebound.root, "p1"));
        QVERIFY(findNode(rebound.root, "p1")->kind == Node::Kind::Panel);
        QCOMPARE(leafIds(rebound.root), (Strings{"p1", "p2"}));
        QCOMPARE(rebound.navigation.frame, (Strings{structuralId, "p1"}));
        QCOMPARE(rebound.navigation.framings.size(), size_t(1));
        QCOMPARE(rebound.navigation.framings.front().id, std::string("bookmark"));
        QCOMPARE(rebound.navigation.framings.front().frame, (Strings{structuralId}));
        QCOMPARE(stage ? rebound.hidden.front().restore.panel : rebound.hidden.front().restore.beside, structuralId);
        if (stage)
            QCOMPARE(findStage(rebound.root)->child->id, std::string("group"));
        QCOMPARE(saved.root->id, std::string("p1"));
    }

    void jsonRoundtripPreservesIndependentCanvasesAndNavigation()
    {
        const auto original = sample();
        const auto bytes = encodeSession(original);
        QString error;
        const auto decoded = decodeSession(bytes, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error));
        QCOMPARE(decoded->size(), size_t(2));
        QCOMPARE((*decoded)[0].output, original[0].output);
        QCOMPARE((*decoded)[1].output, original[1].output);
        QVERIFY((*decoded)[0].windows == original[0].windows);
        QCOMPARE((*decoded)[0].document.root->weights, original[0].document.root->weights);
        QCOMPARE((*decoded)[0].document.navigation.frame, original[0].document.navigation.frame);
        QCOMPARE((*decoded)[0].document.navigation.framings[0].id, std::string("view-work"));
        QCOMPARE((*decoded)[0].document.hidden.size(), size_t(3));
        QVERIFY(sameRect((*decoded)[0].document.floating.front().rect, original[0].document.floating.front().rect));
        QCOMPARE(encodeSession(*decoded), bytes);
        const auto empty = decodeSession(encodeSession({}));
        QVERIFY(empty);
        QVERIFY(empty->empty());
    }

    void captionsAndProcessMetadataDoNotAffectEncodingOrMatching()
    {
        auto before = sample();
        auto after = before;
        for (auto &canvas : after)
            for (auto &[view, record] : canvas.document.views) {
                record.title = "DIFFERENT_PRIVATE_CAPTION";
                record.params = {{"pid", "DIFFERENT_PRIVATE_PID"}, {"caption", "PRIVATE_PARAMETER"}};
            }
        const auto bytes = encodeSession(before);
        QCOMPARE(bytes, encodeSession(after));
        QVERIFY(!bytes.contains("PRIVATE_CAPTION_TOKEN"));
        QVERIFY(!bytes.contains("PRIVATE_PID_TOKEN"));
        QVERIFY(!bytes.contains("\"title\""));
        QVERIFY(!bytes.contains("\"caption\""));
        QVERIFY(!bytes.contains("\"pid\""));
        QVERIFY(!bytes.contains("\"params\""));
        std::vector<WindowIdentity> savedKeys, liveKeys;
        for (const auto &[panel, key] : before.front().windows)
            savedKeys.push_back(key);
        for (const auto &[panel, key] : after.front().windows)
            liveKeys.insert(liveKeys.begin(), key);
        const auto matches = uniqueWindowMatches(savedKeys, liveKeys);
        QCOMPARE(matches.size(), savedKeys.size());
        for (size_t i = 0; i < savedKeys.size(); ++i)
            QCOMPARE(matches.at(i), savedKeys.size() - 1 - i);
    }

    void malformedJsonIsRejected_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        auto row = [](const char *name, QByteArray bytes) { QTest::newRow(name) << bytes; };
        row("truncated", QByteArray("{\"schema\":1,"));
        row("top-level-array", QByteArray("[]"));
        row("too-large", QByteArray(2 * 1024 * 1024 + 1, ' '));
        row("future-schema", editRoot([](QJsonObject &o) { o["schema"] = 2; }));
        row("schema-type", editRoot([](QJsonObject &o) { o["schema"] = "1"; }));
        row("schema-missing", editRoot([](QJsonObject &o) { o.remove("schema"); }));
        row("canvases-not-array", editRoot([](QJsonObject &o) { o["canvases"] = QJsonObject{}; }));
        row("duplicate-output", editRoot([](QJsonObject &o) {
            auto canvases = o["canvases"].toArray();
            auto second = canvases[1].toObject();
            second["output"] = canvases[0].toObject()["output"];
            canvases[1] = second;
            o["canvases"] = canvases;
        }));
        row("too-deep", editCanvas([](QJsonObject &o) {
            QJsonValue nested = QJsonObject{{"kind", "panel"}, {"id", "editor"}, {"views", QJsonArray{"editor-view"}}};
            for (int i = 0; i < 70; ++i)
                nested = QJsonObject{{"kind", "stage"}, {"id", QString("level-%1").arg(i)}, {"child", nested}};
            o["root"] = nested;
        }));
        row("unknown-node-kind", editSplit([](QJsonObject &o) { o["kind"] = "unknown"; }));
        row("invalid-axis", editSplit([](QJsonObject &o) { o["axis"] = "z"; }));
        row("weight-count", editSplit([](QJsonObject &o) { o["weights"] = QJsonArray{1}; }));
        row("zero-weight", editSplit([](QJsonObject &o) { o["weights"] = QJsonArray{0, 1}; }));
        row("negative-weight", editSplit([](QJsonObject &o) { o["weights"] = QJsonArray{-1, 2}; }));
        row("nonnumeric-weight", editSplit([](QJsonObject &o) { o["weights"] = QJsonArray{"0.35", 0.65}; }));
        row("nonfinite-weight", editSplit([](QJsonObject &o) {
            o["weights"] = QJsonArray{QJsonValue(std::numeric_limits<double>::infinity()), 1};
        }));
        row("overflow-weight", QByteArray("{\"schema\":1,\"canvases\":[{\"output\":\"screen\",\"root\":{\"id\":\"row\",\"kind\":\"split\",\"axis\":\"x\",\"children\":[{\"id\":\"p\",\"kind\":\"panel\",\"views\":[\"v\"]}],\"weights\":[1e999]},\"floating\":[],\"hidden\":[],\"windows\":[{\"panel\":\"p\",\"app\":\"app\",\"role\":\"\"}],\"navigation\":{\"frame\":[],\"framings\":[]}}]}"));
        row("duplicate-node-id", editCanvas([](QJsonObject &o) {
            auto floats = o["floating"].toArray();
            auto f = floats[0].toObject();
            auto p = f["panel"].toObject();
            p["id"] = "editor";
            f["panel"] = p; floats[0] = f; o["floating"] = floats;
        }));
        row("duplicate-view-id", editCanvas([](QJsonObject &o) {
            auto floats = o["floating"].toArray();
            auto f = floats[0].toObject();
            auto p = f["panel"].toObject();
            p["views"] = QJsonArray{"editor-view"};
            f["panel"] = p; floats[0] = f; o["floating"] = floats;
        }));
        row("empty-panel", editSplit([](QJsonObject &o) {
            auto children = o["children"].toArray();
            auto p = children[0].toObject();
            p["views"] = QJsonArray{};
            children[0] = p; o["children"] = children;
        }));
        row("duplicate-window-slot", editCanvas([](QJsonObject &o) {
            auto windows = o["windows"].toArray(); windows.append(windows[0]); o["windows"] = windows;
        }));
        row("missing-window-identity", editCanvas([](QJsonObject &o) {
            auto windows = o["windows"].toArray(); windows.removeAt(0); o["windows"] = windows;
        }));
        row("unknown-window-panel", editCanvas([](QJsonObject &o) {
            auto windows = o["windows"].toArray();
            auto w = windows[0].toObject(); w["panel"] = "unplaced";
            windows[0] = w; o["windows"] = windows;
        }));
        row("duplicate-framing-id", editCanvas([](QJsonObject &o) {
            auto nav = o["navigation"].toObject();
            auto framings = nav["framings"].toArray(); framings.append(framings[0]);
            nav["framings"] = framings; o["navigation"] = nav;
        }));
        row("invalid-floating-width", editCanvas([](QJsonObject &o) {
            auto floats = o["floating"].toArray(); auto f = floats[0].toObject();
            f["rect"] = QJsonArray{0, 0, -0.1, 0.2}; floats[0] = f; o["floating"] = floats;
        }));
        row("nonfinite-floating-coordinate", editCanvas([](QJsonObject &o) {
            auto floats = o["floating"].toArray(); auto f = floats[0].toObject();
            f["rect"] = QJsonArray{QJsonValue(std::numeric_limits<double>::quiet_NaN()), 0, 0.2, 0.2};
            floats[0] = f; o["floating"] = floats;
        }));
        row("invalid-hidden-share", editCanvas([](QJsonObject &o) {
            auto hidden = o["hidden"].toArray(); auto h = hidden[0].toObject();
            auto restore = h["restore"].toObject(); restore["share"] = -0.1;
            h["restore"] = restore; hidden[0] = h; o["hidden"] = hidden;
        }));
        row("fractional-floating-layer", editCanvas([](QJsonObject &o) {
            auto floats = o["floating"].toArray(); auto f = floats[0].toObject();
            f["layer"] = 0.5; floats[0] = f; o["floating"] = floats;
        }));
        row("fractional-z", editCanvas([](QJsonObject &o) {
            auto floats = o["floating"].toArray(); auto f = floats[0].toObject();
            f["z"] = 1.5; floats[0] = f; o["floating"] = floats;
        }));
        row("fractional-restore-kind", editCanvas([](QJsonObject &o) {
            auto hidden = o["hidden"].toArray(); auto h = hidden[0].toObject();
            auto restore = h["restore"].toObject(); restore["kind"] = 0.5;
            h["restore"] = restore; hidden[0] = h; o["hidden"] = hidden;
        }));
    }

    void malformedJsonIsRejected()
    {
        QFETCH(QByteArray, bytes);
        QString error;
        QVERIFY(!decodeSession(bytes, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSession)
#include "test_session.moc"
