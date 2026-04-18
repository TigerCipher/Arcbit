#pragma once

#include <arcbit/core/Math.h>

namespace Arcbit
{

// Scene-wide constants that systems read instead of hard-coding per-entity.
// Set once per scene in OnStart via GetScene().GetConfig().
struct WorldConfig
{
    f32  TileSize = 32.0f;          // world pixels per tile (used by tile movement systems)
    Vec2 Gravity  = { 0.0f, 0.0f }; // pixels/s² (zero = top-down, no gravity)
};

} // namespace Arcbit
