// A stand-in spring for the KWin-free test suite.
//
// Production motion is KWin's own simulation (effect/kwin_spring.cpp wraps KWin::SpringMotion), and krellis_core has
// no implementation of its own. The core suite must keep building and running without KWin, so the RectSpring and
// Navigator tests inject this plain integrator instead: it solves the same mass-spring-damper equation with the same
// Motion::stiffness / Motion::damping constants, which is all those tests need to check the spring's API and the
// camera state machine around it. Feel is covered by the effect's scenarios, not here.
#pragma once

#include "motion.h"

namespace krellis
{

SpringFactory testSpringFactory();

} // namespace krellis
