#include "view_rules.h"
#include <QtTest>
using namespace trellis;
class TestViewRules : public QObject {
    Q_OBJECT
private slots:
    void scalingInteractivity() {
        ViewRules r;
        QVERIFY(r.interactive(0.1));
        r.scaling = ViewScaling::Inert;
        QVERIFY(!r.interactive(0.5));
        QVERIFY(!r.interactive(0.998));
        QVERIFY(r.interactive(0.999));
        QVERIFY(r.interactive(2));
        r.scaling = ViewScaling::None;
        QVERIFY(r.interactive(0.1));
    }
    void independentPlacementPermissions() {
        ViewRules r;
        QVERIFY(r.allows(ViewPlacement::Stage));
        QVERIFY(r.allows(ViewPlacement::Side));
        QVERIFY(r.allows(ViewPlacement::Floating));
        r.side = false; r.floating = false;
        QVERIFY(!r.allows(ViewPlacement::Side));
        QVERIFY(!r.allows(ViewPlacement::Floating));
        QVERIFY(r.allows(ViewPlacement::Stage));
    }
    void iconThresholds() {
        QVERIFY(iconTile({0,0,159.9,100}));
        QVERIFY(iconTile({0,0,200,63.9}));
        QVERIFY(!iconTile({0,0,160,64}));
        QVERIFY(!iconTile({-200,30,640,400}));
    }
};
QTEST_APPLESS_MAIN(TestViewRules)
#include "test_view_rules.moc"
