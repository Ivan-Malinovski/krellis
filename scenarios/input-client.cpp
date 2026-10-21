// Actual Wayland pointer delivery fixture; coordinates are client-local.
#include <QApplication>
#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <iostream>
class Client : public QWidget {
public:
    explicit Client(QWidget *parent = nullptr, Qt::WindowFlags flags = {}) : QWidget(parent, flags) {
        setMouseTracking(true);
        setWindowTitle(parent ? "input-popup" : "input-client");
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        std::cout << (isWindow() && parentWidget() ? "POP_MOVE " : "MOVE ") << event->position().x() << ' ' << event->position().y() << std::endl;
    }
    void mousePressEvent(QMouseEvent *event) override {
        const bool popup = parentWidget();
        std::cout << (popup ? "POP_CLICK " : "CLICK ") << event->position().x() << ' ' << event->position().y() << std::endl;
        if (!popup && event->position().x() < 60 && event->position().y() < 60) {
            auto *menu = new Client(this, Qt::Popup);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->resize(200, 100);
            menu->move(mapToGlobal(QPoint(100, 80)));
            menu->show();
        }
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        std::cout << (parentWidget() ? "POP_RELEASE " : "RELEASE ") << event->position().x() << ' ' << event->position().y() << std::endl;
    }
    void wheelEvent(QWheelEvent *event) override {
        std::cout << "WHEEL " << event->position().x() << ' ' << event->position().y() << std::endl;
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), parentWidget() ? QColor("#8d557a") : QColor("#26394e"));
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, parentWidget() ? "Popup: scaled with parent" : "Pointer coordinates recorded to fixture log");
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setDesktopFileName(argc > 1 ? QString::fromUtf8(argv[1]) : QStringLiteral("trellis-input-fixture"));
    Client client;
    client.setMinimumSize(app.arguments().contains("--small-min") ? QSize(1, 1) : QSize(640, 400));
    client.resize(640, 480);
    client.show();
    return app.exec();
}
