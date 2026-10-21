// A client that takes 250 ms to lay out after every resize, like a heavy browser window.
#include <QApplication>
#include <QResizeEvent>
#include <QThread>
#include <QWidget>

struct Slow : QWidget {
    void resizeEvent(QResizeEvent *e) override { if (e->oldSize().isValid()) QThread::msleep(250); }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Slow w;
    w.setWindowTitle(argc > 1 ? argv[1] : "slow");
    w.resize(400, 300);
    w.show();
    return app.exec();
}
