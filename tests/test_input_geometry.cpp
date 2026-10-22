#include "input_geometry.h"
#include <QtTest>
using namespace krellis;
class TestInputGeometry : public QObject {
    Q_OBJECT
private slots:
    void pointerRoundTrip() {
        const Rect rest{1286, 36, 1000, 700}, shown{1600, 100, 250, 350};
        const auto p = mapPoint({1700, 275}, shown, rest);
        QVERIFY(p.has_value());
        QCOMPARE(p->x, 1686.0); QCOMPARE(p->y, 386.0);
        const auto back = mapPoint(*p, rest, shown);
        QCOMPARE(back->x, 1700.0); QCOMPARE(back->y, 275.0);
    }
    void popupUsesParentOrigin() {
        const auto popup = mapChildRect({1400, 100, 200, 100}, {1200, 20, 1000, 800}, {-400, 30, 500, 400});
        QVERIFY(popup.has_value());
        QCOMPARE(popup->x, -300.0); QCOMPARE(popup->y, 70.0);
        QCOMPARE(popup->w, 100.0); QCOMPARE(popup->h, 50.0);
        const auto nested = mapChildRect({1500, 120, 50, 40}, {1400, 100, 200, 100}, *popup);
        QCOMPARE(nested->x, -250.0); QCOMPARE(nested->y, 80.0);
    }
    void grabMayLeaveBounds() {
        const auto point = mapPoint({-20, 150}, {0, 0, 100, 100}, {20, 40, 400, 200});
        QCOMPARE(point->x, -60.0); QCOMPARE(point->y, 340.0);
    }
    void emptyCannotTakeInput() {
        QVERIFY(!mapPoint({1, 2}, {0, 0, 0, 100}, UNIT));
        QVERIFY(!mapPoint({1, 2}, UNIT, {0, 0, 100, 0}));
        QVERIFY(!mapChildRect(UNIT, {0, 0, -1, 1}, UNIT));
    }
};
QTEST_APPLESS_MAIN(TestInputGeometry)
#include "test_input_geometry.moc"
