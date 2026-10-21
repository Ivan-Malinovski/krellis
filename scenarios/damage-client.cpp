// Solid-colour surface whose idle timer damages only a small cursor-like patch.
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <QWidget>
#include <iostream>

class Client : public QWidget {
public:
    explicit Client(QColor color) : m_color(color) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMinimumSize(1, 1);
        auto *timer = new QTimer(this);
        QObject::connect(timer, &QTimer::timeout, this, [this] { m_tick = !m_tick; update(QRect(10, 10, 18, 18)); });
        timer->start(100);
    }
    void paintEvent(QPaintEvent *event) override {
        QPainter p(this);
        p.fillRect(event->rect(), m_color);
        p.fillRect(QRect(10, 10, 18, 18), m_tick ? Qt::yellow : Qt::magenta);
    }
    void mousePressEvent(QMouseEvent *event) override {
        std::cout << "CLICK " << event->position().x() << ' ' << event->position().y() << std::endl;
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        std::cout << "RELEASE " << event->position().x() << ' ' << event->position().y() << std::endl;
    }
private:
    QColor m_color;
    bool m_tick = false;
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 3) return 1;
    app.setDesktopFileName(QString::fromUtf8(argv[1]));
    Client client(QColor(QString::fromUtf8(argv[2])));
    client.setWindowTitle(QString::fromUtf8(argv[1]));
    client.resize(600, 480);
    client.show();
    return app.exec();
}
