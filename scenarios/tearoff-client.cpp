// Stands in for Brave tearing a tab out: pressing in the window opens a second window and at once asks the
// compositor to start moving it (xdg_toplevel.move), so the new window is dragged before it has been placed.
#include <QApplication>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>
#include <QWindow>

struct Source : QWidget {
    QWidget *torn = nullptr;
    void mousePressEvent(QMouseEvent *) override
    {
        if (torn)
            return;
        torn = new QWidget;
        torn->setWindowTitle("tear-new");
        torn->resize(500, 350);
        torn->show();
        QTimer::singleShot(250, torn, [this] { torn->windowHandle()->startSystemMove(); });
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Source w;
    w.setWindowTitle("tear-src");
    w.resize(400, 300);
    w.show();
    return app.exec();
}
