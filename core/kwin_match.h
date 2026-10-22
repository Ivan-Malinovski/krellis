// Whether the plugin may load into the KWin that is running. It uses KWin's private C++ API, which can change in any
// release, so the only safe KWin is the exact X.Y.Z it was built against.
#pragma once

#include <string>

namespace krellis {

/// The leading "X.Y.Z" of a version string: digits and dots, so "6.7.5", "6.7.5-1" and "6.7.5.1+git" all give the first.
inline std::string kwinCoreVersion(const std::string &version)
{
    std::string out;
    for (char c : version) {
        if ((c >= '0' && c <= '9') || c == '.')
            out += c;
        else
            break;
    }
    while (!out.empty() && out.back() == '.')
        out.pop_back();
    return out;
}

/// An unknown running version never locks the effect out: the check exists to stop a known mismatch, not to guess.
inline bool kwinVersionMatches(const std::string &built, const std::string &running)
{
    const std::string have = kwinCoreVersion(running);
    const std::string want = kwinCoreVersion(built);
    if (have.empty() || want.empty())
        return true;
    return have == want;
}

} // namespace krellis
