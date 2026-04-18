#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>

#include <vector>

namespace Arcbit {

// Three draw layers for every tile position.
enum class TileLayer : u8
{
    Ground  = 0, // always behind entities (fixed layer -1000)
    Objects = 1, // Y-sorted with entities
    Overlay = 2, // always in front (fixed layer 1000000)
};

// One frame in a flip-book tile animation.
struct TileAnimFrame
{
    u32 TileX      = 0;
    u32 TileY      = 0;
    u32 DurationMs = 150;
};

// Per-tile behaviour overrides.  Register with TileMap::RegisterTile().
// Tiles without a registered def render at their default atlas UV.
struct TileDef
{
    bool Solid = false; // blocks pathfinding / collision queries

    // Flip-book animation — cycles through listed frames in order.
    // Uses a per-position phase offset so adjacent tiles don't march in lockstep.
    // Mutually exclusive with UVScroll.
    std::vector<TileAnimFrame> Animation;

    // Continuous UV offset applied each frame: UV += scroll * elapsed (seconds).
    // Sampler repeat mode wraps the offset so the texture tiles seamlessly.
    // Mutually exclusive with Animation.
    Vec2 UVScroll = {};
};

} // namespace Arcbit
