#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/input/InputTypes.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Controller components — decide where an entity wants to go.
// Pair with a style component to control how it gets there.
// ---------------------------------------------------------------------------

// Reads InputManager actions each tick and writes a movement intent into
// whichever style component is present on the same entity.
struct InputMovement
{
    ActionID MoveLeft;
    ActionID MoveRight;
    ActionID MoveUp;
    ActionID MoveDown;
    f32      Speed = 100.0f;  // world pixels/s (FreeMovement) or tiles/s (tile movement)
};

// Driven externally by the pathfinding system. Translates TargetPosition into
// the same movement intent as InputMovement.
struct AIMovement
{
    Vec2 TargetPosition;
    f32  Speed     = 100.0f;
    bool HasTarget = false;
};

// ---------------------------------------------------------------------------
// Style components — decide how the entity moves.
// ---------------------------------------------------------------------------

// Pixel-precise velocity integration with exponential friction decay.
// Use for combat, platformers, or any sub-tile positioning.
struct FreeMovement
{
    Vec2 Velocity;
    f32  Friction = 8.0f;   // velocity multiplied by exp(-Friction * dt) each tick
    f32  MaxSpeed = 0.0f;   // clamp magnitude after integration; 0 = unlimited
};

// Lerps from tile centre to tile centre. Input queued during transit.
struct SmoothTileMovement
{
    f32  Speed      = 4.0f;   // tiles/second; tile size comes from WorldConfig
    Vec2 OriginWorld;
    Vec2 TargetWorld;
    f32  Progress   = 1.0f;   // 0→1 lerp progress; 1.0 = arrived
    Vec2 QueuedDir;
    bool HasQueued  = false;
};

// Moves instantly to the adjacent tile. No interpolation.
struct SnapTileMovement {};

// ---------------------------------------------------------------------------
// Resolver plumbing
// ---------------------------------------------------------------------------

// Tentative motion for the current tick. Movement systems (FreeMovement, and
// in 22C SmoothTileMovement) write a desired world-space delta here;
// CollisionResolutionSystem consumes it, clamps via swept narrowphase, and
// commits the final motion to Transform2D, then zeroes the field.
//
//   - FreeMovement entities **must** also have PendingMove — otherwise
//     velocity decays from friction but the entity never actually moves.
//
// The Phase 22B.3 stub resolver just copies DesiredDelta to Transform2D
// unmodified; the real resolver (Phase 22B.4) clamps it against colliders.
struct PendingMove
{
    Vec2 DesiredDelta = {};
};

} // namespace Arcbit
