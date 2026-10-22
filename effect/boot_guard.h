// Crash-loop breaker plumbing: reads and writes the boot marker that core/safemode.h judges. Nothing here needs KWin
// objects, so the factory can call it before an effect exists.
#pragma once

namespace KWin {

/// The factory's isSupported(): false when the previous runs kept dying soon after loading. It is called on every load and
/// every support query, so it only reads, except that the first refusal leaves a flag and sends the notification once.
bool krellisBootAllowed();
/// The effect's constructor: records this start, counting an early death of the previous run as a strike.
void krellisBootStarted();
/// The effect lived long enough to count as healthy.
void krellisBootSurvived();
/// Unloaded normally (destructor): the next start begins from zero.
void krellisBootClean();

} // namespace KWin
