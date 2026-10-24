// A small window with the traits of the helpers Krellis must not tile. Usage: small-client <fixed|bar|plain> <title>
#include <QApplication>
#include <QLabel>
#include <QString>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString mode = argc > 1 ? argv[1] : "plain";
    QLabel w(argc > 2 ? argv[2] : "small");
    w.setAlignment(Qt::AlignCenter);
    if (mode == "fixed") {
        w.setFixedSize(360, 180); // a wallet prompt: decorated, cannot be resized
    } else if (mode == "bar") {
        w.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool); // the "sharing this window" bar
        w.resize(420, 48);
    } else {
        w.resize(360, 180); // small but an ordinary resizable window: it still tiles
    }
    w.show();
    return app.exec();
}
