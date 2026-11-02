#include "kwin_spring.h"

namespace krellis
{

SpringFactory kwinSpringFactory()
{
    return [] { return static_cast<Spring1D *>(new KwinSpring); };
}

} // namespace krellis
