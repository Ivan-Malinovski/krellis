// Nested-harness fixture: a real Wayland exclusive-zone panel.
#include <QGuiApplication>
#include <QRasterWindow>
#include <QPainter>
#include <LayerShellQt/Window>
class Panel : public QRasterWindow {
    void paintEvent(QPaintEvent *) override { QPainter p(this); p.fillRect(QRect(QPoint(), size()), QColor("#182234")); }
};
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    Panel panel;
    using W = LayerShellQt::Window;
    auto *layer = W::get(&panel);
    if (app.arguments().size() > 2) {
        const int output = app.arguments().at(2).toInt();
        if (output < 0 || output >= app.screens().size()) return 2;
        layer->setScreen(app.screens().at(output));
    }
    const bool top = app.arguments().contains("top");
    layer->setAnchors(W::Anchors(W::AnchorLeft) | W::AnchorRight | (top ? W::AnchorTop : W::AnchorBottom));
    layer->setLayer(W::LayerTop);
    layer->setExclusiveZone(64);
    layer->setKeyboardInteractivity(W::KeyboardInteractivityNone);
    panel.resize(1280, 64);
    panel.show();
    return app.exec();
}
