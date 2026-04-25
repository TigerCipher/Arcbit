#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/physics/AABB.h>
#include <arcbit/physics/Collider2D.h>

namespace Arcbit::Narrowphase
{
// Pure-math overlap tests. Pair lookups come from the broadphase
// (PhysicsWorld::QueryAABB → SpatialHash); these functions decide whether
// a candidate pair *actually* touches.
//
// All tests are boolean only — contact manifolds (normal, depth, contact
// point) land in Phase 22B alongside the FreeMovement resolver.
//
// Coordinates are world-space. The dispatcher takes a `Vec2 worldPosition`
// for each collider; the collider's local Offset is added internally.

[[nodiscard]] bool AABBOverlap(const AABB& a, const AABB& b) noexcept;

// True iff the AABB and the circle defined by (center, radius) overlap.
[[nodiscard]] bool AABBCircleOverlap(const AABB& box, Vec2 center, f32 radius) noexcept;

// True iff the two discs overlap.
[[nodiscard]] bool CircleOverlap(Vec2 centerA, f32 radiusA,
                                 Vec2 centerB, f32 radiusB) noexcept;

// Dispatcher: picks the right specialized test based on shape combination.
//
// Box-vs-Box and Box-vs-Circle use the AABB fast path when both boxes have
// `Rotation == 0`. Rotated boxes fall back to a *conservative* bounding-AABB
// test for now — the OBB-accurate SAT path lands in Phase 22E. Conservative
// here means "may report a collision that isn't there", never "may miss one";
// false positives are caught by the resolver and at worst cost a redundant
// resolution step.
[[nodiscard]] bool Overlap(const Collider2D& a, Vec2 worldPosA,
                           const Collider2D& b, Vec2 worldPosB) noexcept;

// Debug-only round-trip self test.
void SelfTest();
} // namespace Arcbit::Narrowphase
