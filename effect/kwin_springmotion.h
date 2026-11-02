// The ABI Krellis links against for KWin's own spring simulation.
//
// KWin 6.8 moved the mass-spring-damper used by its slide/overview animations into the core effect API
// (src/effect/springmotion.{h,cpp}) and exports it from libkwin, but the header is not on KWin's install list
// (src/CMakeLists.txt lists the installed effect headers and omits it, so /usr/include/kwin/effect/ has none).
// An effect therefore cannot #include it. Nothing here is copied from KWin: this file declares only the interface
// that the exported symbols already encode, plus a correctly sized opaque body.
//
// This owns a coupling that has to be re-checked on every KWin upgrade: the size assertion below catches a change
// in the object layout, and Krellis is ABI-locked to one KWin anyway (it refuses to load into another one), so a
// rebuild per KWin is already mandatory. If KWin ever ships the header, drop this file and include theirs.
#pragma once

#include <QtGlobal>

#include <chrono>
#include <type_traits>

namespace KWin
{

class SpringMotion
{
public:
    SpringMotion();
    SpringMotion(qreal springConstant, qreal dampingRatio);

    void advance(std::chrono::milliseconds delta);
    bool isMoving() const;

    qreal velocity() const;
    void setVelocity(qreal velocity);
    qreal position() const;
    void setPosition(qreal position);
    qreal anchor() const;
    void setAnchor(qreal anchor);

    qreal springConstant() const;
    qreal dampingRatio() const;
    qreal epsilon() const;
    void setEpsilon(qreal epsilon);

private:
    // KWin 6.8.2's layout: two (position, velocity) pairs and seven qreal; verified with sizeof() against KWin's
    // own header. Krellis never reads these bytes, only the class's size matters.
    alignas(double) unsigned char m_body[88];
};

static_assert(sizeof(SpringMotion) == 88, "KWin::SpringMotion changed size: re-check effect/kwin_springmotion.h against this KWin");
static_assert(!std::is_polymorphic_v<SpringMotion>, "KWin::SpringMotion must stay non-polymorphic");
static_assert(std::is_trivially_destructible_v<SpringMotion>, "KWin::SpringMotion must stay trivially destructible");

} // namespace KWin
