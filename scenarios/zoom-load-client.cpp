// Many independent Wayland surfaces in one quiet process, to isolate compositor costs.
#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QWidget>
#include <iostream>
#include <memory>
#include <vector>

class Tile : public QWidget {
public:
    explicit Tile(int index) : m_index(index) {
        setWindowTitle(QStringLiteral("zoom-load-%1").arg(index));
        setMinimumSize(160, 120);
        resize(480, 320);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor::fromHsv((m_index * 47) % 360, 100, 100));
        if (QApplication::arguments().contains(QStringLiteral("--axis-markers"))) {
            p.fillRect(QRect(100, 0, 10, height()), Qt::cyan);
            p.fillRect(QRect(0, 100, width(), 10), Qt::magenta);
        }
        // A right-anchored stripe moves within the window whenever its width changes: a new layout that replaces
        // the old one in a single presented frame shows up as the stripe jumping.
        if (QApplication::arguments().contains(QStringLiteral("--right-marker")))
            p.fillRect(QRect(width() - 150, 0, 24, height()), Qt::yellow);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, windowTitle());
    }
    void mousePressEvent(QMouseEvent *event) override {
        std::cout << "CLICK " << m_index << ' ' << event->position().x() << ' ' << event->position().y() << std::endl;
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        std::cout << "RELEASE " << m_index << ' ' << event->position().x() << ' ' << event->position().y() << std::endl;
    }
private:
    int m_index;
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setDesktopFileName(QStringLiteral("krellis-zoom-load"));
    std::vector<std::unique_ptr<Tile>> windows;
    for (int i = 0; i < app.arguments().value(1, "25").toInt(); ++i) {
        windows.push_back(std::make_unique<Tile>(i));
        windows.back()->show();
        app.processEvents();
    }
    return app.exec();
}
