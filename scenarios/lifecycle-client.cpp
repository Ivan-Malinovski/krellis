// Small native fixture for enable/disable lifecycle checks in the nested compositor.
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString name = argc > 1 ? QString::fromUtf8(argv[1]) : QStringLiteral("lifecycle-window");
    app.setDesktopFileName(name);
    QWidget window;
    window.setWindowTitle(argc > 2 ? QString::fromUtf8(argv[2]) : name);
    const bool largeMinimum = argc > 3 && QString::fromUtf8(argv[3]) == QStringLiteral("--large-min");
    window.setMinimumSize(largeMinimum ? 1200 : 1, largeMinimum ? 1000 : 1);
    window.resize(480, 320);
    window.show();
    return app.exec();
}
