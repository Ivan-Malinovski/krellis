#pragma once
#include "session.h"
#include <QByteArray>
#include <QString>
namespace trellis {
struct SavedCanvas {
    std::string output;
    LayoutDocument document;
    std::map<std::string, WindowIdentity> windows; // panel ID -> identity
};
using SessionSnapshot = std::vector<SavedCanvas>;
QByteArray encodeSession(const SessionSnapshot &session);
std::optional<SessionSnapshot> decodeSession(const QByteArray &bytes, QString *error = nullptr);
}
