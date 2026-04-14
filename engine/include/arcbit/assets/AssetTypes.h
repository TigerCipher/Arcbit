#pragma once

#include <arcbit/core/Types.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// UVRect
//
// Normalized (0-1) texture coordinates for a rectangular region of a texture.
// U0/V0 is the top-left corner; U1/V1 is the bottom-right corner.
//
// Used by SpriteSheet to describe individual sprite frames, and by the sprite
// batcher (Phase 14) to map world-space quads to atlas sub-regions.
// ---------------------------------------------------------------------------
struct UVRect
{
    f32 U0 = 0.0f, V0 = 0.0f; // top-left
    f32 U1 = 1.0f, V1 = 1.0f; // bottom-right
};

} // namespace Arcbit
