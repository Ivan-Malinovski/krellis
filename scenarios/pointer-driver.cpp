// Drive the real input pipeline of a task-owned nested compositor only.
#include <KWayland/Client/registry.h>
#include <KWayland/Client/fakeinput.h>
#include <QCoreApplication>
#include <QPointF>
#include <wayland-client.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const std::string socket = qEnvironmentVariable("NEST_SOCKET").toStdString();
    const auto work = qEnvironmentVariable("NEST_WORK");
    if (!socket.starts_with("trellis-test-") || qEnvironmentVariable("WAYLAND_DISPLAY").toStdString() != socket
        || work.isEmpty() || qEnvironmentVariable("XDG_CONFIG_HOME") != work + "/config") {
        std::cerr << "Pointer fixture requires an isolated trellis-test-* compositor\n";
        return 1;
    }
    auto *display = wl_display_connect(socket.c_str());
    if (!display) return 2;
    KWayland::Client::Registry registry;
    KWayland::Client::FakeInput *pointer = nullptr;
    QObject::connect(&registry, &KWayland::Client::Registry::fakeInputAnnounced, &app,
        [&](quint32 name, quint32 version) { pointer = registry.createFakeInput(name, version, &registry); });
    registry.create(display);
    registry.setup();
    if (wl_display_roundtrip(display) < 0 || !pointer || !pointer->isValid()) return 3;
    pointer->authenticate(QStringLiteral("Trellis nested regression"), QStringLiteral("Decoration input test in isolated compositor"));
    if (wl_display_roundtrip(display) < 0) return 4;
    std::cout << "READY" << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        input >> command;
        if (command == "move") {
            double x, y;
            if (!(input >> x >> y)) return 5;
            pointer->requestPointerMoveAbsolute(QPointF(x, y));
        } else if (command == "key") {
            quint32 key;
            if (!(input >> key)) return 5;
            pointer->requestKeyboardKeyPress(key);
            pointer->requestKeyboardKeyRelease(key);
        } else if (command == "down") pointer->requestPointerButtonPress(Qt::LeftButton);
        else if (command == "up") pointer->requestPointerButtonRelease(Qt::LeftButton);
        else if (command == "quit") break;
        else return 6;
        if (wl_display_roundtrip(display) < 0) return 7;
        std::cout << "OK" << std::endl;
    }
    // Destroy the protocol objects before disconnecting their display.
    pointer->release();
    registry.release();
    wl_display_flush(display);
    wl_display_disconnect(display);
    return 0;
}
