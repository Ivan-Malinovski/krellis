// Crash-loop breaker. The effect leaves a marker when it loads and marks it clean when it unloads normally; a marker
// that is neither clean nor "survived" means KWin died soon after the effect loaded.
#pragma once

#include <cstdint>

namespace krellis {

struct BootMarker {
    std::int64_t startedAtMs = 0; // wall clock
    bool cleanExit = true;        // the destructor ran
    bool survived = false;        // the session lasted long enough to count as healthy
    int strikes = 0;              // consecutive early deaths up to and including the one that left this marker
};

struct BootDecision {
    bool allow = true;
    int strikes = 0;
};

constexpr int BOOT_STRIKE_LIMIT = 2;
constexpr std::int64_t BOOT_SURVIVE_MS = 120'000;      // living this long counts as healthy
constexpr std::int64_t BOOT_FRESH_MS = 10 * 60'000;    // deaths further apart than this are not one crash loop

/// Whether to load, given the previous run's marker. A missing or unreadable marker is a default `BootMarker`.
/// A death long after the previous start counts as a single strike again, so a loop that has been blocked gets one retry
/// at the next login instead of staying off for ever.
inline BootDecision decideBoot(const BootMarker &prev, std::int64_t nowMs)
{
    const bool died = !prev.cleanExit && !prev.survived;
    if (!died)
        return {true, 0};
    const std::int64_t age = nowMs - prev.startedAtMs;
    const bool sameBurst = age >= 0 && age <= BOOT_FRESH_MS;
    const int strikes = sameBurst ? prev.strikes + 1 : 1;
    return {strikes < BOOT_STRIKE_LIMIT, strikes};
}

} // namespace krellis
