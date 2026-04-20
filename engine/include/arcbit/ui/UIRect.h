#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/core/Math.h>

namespace Arcbit
{
// Axis-aligned screen-space rect in pixels (top-left origin).
struct UIRect
{
    f32 X = 0, Y = 0, W = 0, H = 0;

    [[nodiscard]] bool Contains(const Vec2 p) const
    {
        return p.X >= X && p.X < X + W && p.Y >= Y && p.Y < Y + H;
    }
};
} // namespace Arcbit
