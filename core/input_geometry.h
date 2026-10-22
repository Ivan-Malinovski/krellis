#pragma once
#include "spatial.h"
#include <optional>

namespace krellis {
// Map between rectangles with independent x/y scale. Empty rectangles cannot
// define an inverse; callers must refuse input rather than divide by zero.
inline std::optional<Point> mapPoint(Point p, const Rect &from, const Rect &to)
{
    if (from.w <= 0 || from.h <= 0 || to.w <= 0 || to.h <= 0) return std::nullopt;
    return Point{to.x + (p.x - from.x) * to.w / from.w,
                 to.y + (p.y - from.y) * to.h / from.h};
}
inline std::optional<Rect> mapChildRect(const Rect &child, const Rect &parentRest, const Rect &parentShown)
{
    const auto origin = mapPoint({child.x, child.y}, parentRest, parentShown);
    if (!origin) return std::nullopt;
    return Rect{origin->x, origin->y, child.w * parentShown.w / parentRest.w, child.h * parentShown.h / parentRest.h};
}
}
