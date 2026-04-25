#include <arcbit/physics/Narrowphase.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/physics/PhysicsWorld.h>

#include <algorithm>

namespace Arcbit::Narrowphase
{
bool AABBOverlap(const AABB& a, const AABB& b) noexcept
{
    return a.Overlaps(b);
}

bool AABBCircleOverlap(const AABB& box, const Vec2 center, const f32 radius) noexcept
{
    // Closest point on the box to the circle's center, then distance squared.
    const f32 cx = std::max(box.Min.X, std::min(center.X, box.Max.X));
    const f32 cy = std::max(box.Min.Y, std::min(center.Y, box.Max.Y));
    const f32 dx = center.X - cx;
    const f32 dy = center.Y - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
}

bool CircleOverlap(const Vec2 centerA, const f32 radiusA,
                   const Vec2 centerB, const f32 radiusB) noexcept
{
    const f32 dx  = centerB.X - centerA.X;
    const f32 dy  = centerB.Y - centerA.Y;
    const f32 sum = radiusA + radiusB;
    return (dx * dx + dy * dy) <= (sum * sum);
}

bool Overlap(const Collider2D& a, const Vec2 worldPosA,
             const Collider2D& b, const Vec2 worldPosB) noexcept
{
    const Vec2 centerA = { worldPosA.X + a.Offset.X, worldPosA.Y + a.Offset.Y };
    const Vec2 centerB = { worldPosB.X + b.Offset.X, worldPosB.Y + b.Offset.Y };

    const bool circleA = (a.Shape == ColliderShape::Circle);
    const bool circleB = (b.Shape == ColliderShape::Circle);

    // Circle vs Circle — exact.
    if (circleA && circleB) return CircleOverlap(centerA, a.Radius, centerB, b.Radius);

    // Box vs Circle — exact when the box is axis-aligned. Rotated boxes use
    // their bounding AABB (conservative; proper OBB lands in 22E).
    if (circleA) {
        const AABB boxAABB = PhysicsWorld::ComputeWorldAABB(b, worldPosB);
        return AABBCircleOverlap(boxAABB, centerA, a.Radius);
    }
    if (circleB) {
        const AABB boxAABB = PhysicsWorld::ComputeWorldAABB(a, worldPosA);
        return AABBCircleOverlap(boxAABB, centerB, b.Radius);
    }

    // Box vs Box — fast path when both are axis-aligned. Rotated boxes use
    // bounding AABBs (conservative).
    const AABB aabbA = PhysicsWorld::ComputeWorldAABB(a, worldPosA);
    const AABB aabbB = PhysicsWorld::ComputeWorldAABB(b, worldPosB);
    return AABBOverlap(aabbA, aabbB);
}

// ---------------------------------------------------------------------------
// SelfTest — overlap correctness on hand-picked positive and negative cases.
// ---------------------------------------------------------------------------

void SelfTest()
{
#ifdef ARCBIT_DEBUG
    // ---- AABB vs AABB --------------------------------------------------------
    constexpr AABB a = AABB::FromCenterHalfExtents({ 0.0f, 0.0f }, { 10.0f, 10.0f });
    constexpr AABB b = AABB::FromCenterHalfExtents({ 5.0f, 5.0f }, { 10.0f, 10.0f });   // overlaps
    constexpr AABB c = AABB::FromCenterHalfExtents({ 30.0f, 0.0f }, { 5.0f, 5.0f });    // disjoint
    constexpr AABB d = AABB::FromCenterHalfExtents({ 20.0f, 0.0f }, { 10.0f, 10.0f });  // edge-touch

    ARCBIT_ASSERT(AABBOverlap(a, b),      "Narrowphase: AABBOverlap missed clear overlap");
    ARCBIT_ASSERT(!AABBOverlap(a, c),     "Narrowphase: AABBOverlap reported false positive");
    ARCBIT_ASSERT(AABBOverlap(a, d),      "Narrowphase: edge-touching AABBs should report overlap");

    // ---- AABB vs Circle ------------------------------------------------------
    constexpr AABB box = AABB::FromCenterHalfExtents({ 0.0f, 0.0f }, { 10.0f, 10.0f });
    // Center inside box → overlap.
    ARCBIT_ASSERT(AABBCircleOverlap(box, { 0.0f, 0.0f }, 1.0f),
                  "Narrowphase: circle inside box must overlap");
    // Center outside box but radius reaches a corner → overlap.
    ARCBIT_ASSERT(AABBCircleOverlap(box, { 12.0f, 12.0f }, 5.0f),
                  "Narrowphase: corner-grazing circle should overlap");
    // Center outside box and radius too small to reach → no overlap.
    ARCBIT_ASSERT(!AABBCircleOverlap(box, { 20.0f, 20.0f }, 5.0f),
                  "Narrowphase: distant circle should not overlap");
    // Edge contact (closest point on box exactly at radius) — counts as overlap (<=).
    ARCBIT_ASSERT(AABBCircleOverlap(box, { 15.0f, 0.0f }, 5.0f),
                  "Narrowphase: just-touching circle and box edge should overlap");

    // ---- Circle vs Circle ----------------------------------------------------
    ARCBIT_ASSERT(CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 4.0f, 0.0f }, 5.0f),
                  "Narrowphase: clear circle overlap missed");
    ARCBIT_ASSERT(!CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 20.0f, 0.0f }, 5.0f),
                  "Narrowphase: distant circles reported as overlapping");
    // Just-touching: distance == r1+r2 → overlap (<=).
    ARCBIT_ASSERT(CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 10.0f, 0.0f }, 5.0f),
                  "Narrowphase: tangent circles should report overlap");

    // ---- Dispatcher ----------------------------------------------------------
    const Collider2D box1{
        .Shape       = ColliderShape::Box,
        .HalfExtents = { 8.0f, 8.0f },
    };
    const Collider2D box2{
        .Shape       = ColliderShape::Box,
        .HalfExtents = { 8.0f, 8.0f },
    };
    const Collider2D circle1{
        .Shape  = ColliderShape::Circle,
        .Radius = 6.0f,
    };

    // Box-vs-Box: positions chosen so AABBs overlap.
    ARCBIT_ASSERT(Overlap(box1, { 0.0f, 0.0f }, box2, { 4.0f, 4.0f }),
                  "Narrowphase: dispatcher missed Box-Box overlap");
    ARCBIT_ASSERT(!Overlap(box1, { 0.0f, 0.0f }, box2, { 100.0f, 100.0f }),
                  "Narrowphase: dispatcher reported false Box-Box overlap");

    // Circle-vs-Box: circle just inside box.
    ARCBIT_ASSERT(Overlap(circle1, { 5.0f, 5.0f }, box1, { 0.0f, 0.0f }),
                  "Narrowphase: dispatcher missed Circle-Box overlap (circle first)");
    ARCBIT_ASSERT(Overlap(box1, { 0.0f, 0.0f }, circle1, { 5.0f, 5.0f }),
                  "Narrowphase: dispatcher missed Circle-Box overlap (box first)");
    ARCBIT_ASSERT(!Overlap(circle1, { 50.0f, 50.0f }, box1, { 0.0f, 0.0f }),
                  "Narrowphase: dispatcher reported false Circle-Box overlap");

    // Circle-vs-Circle through dispatcher.
    const Collider2D circle2{
        .Shape  = ColliderShape::Circle,
        .Radius = 4.0f,
    };
    ARCBIT_ASSERT(Overlap(circle1, { 0.0f, 0.0f }, circle2, { 8.0f, 0.0f }),
                  "Narrowphase: dispatcher missed Circle-Circle overlap");
    ARCBIT_ASSERT(!Overlap(circle1, { 0.0f, 0.0f }, circle2, { 50.0f, 0.0f }),
                  "Narrowphase: dispatcher reported false Circle-Circle overlap");

    // Offset is honored: identical shapes at the same world position but with
    // opposite Offsets push them apart.
    const Collider2D leftBox{
        .Shape       = ColliderShape::Box,
        .Offset      = { -10.0f, 0.0f },
        .HalfExtents = { 4.0f, 4.0f },
    };
    const Collider2D rightBox{
        .Shape       = ColliderShape::Box,
        .Offset      = { 10.0f, 0.0f },
        .HalfExtents = { 4.0f, 4.0f },
    };
    ARCBIT_ASSERT(!Overlap(leftBox, { 0.0f, 0.0f }, rightBox, { 0.0f, 0.0f }),
                  "Narrowphase: dispatcher ignored collider Offset");

    LOG_DEBUG(Engine, "Narrowphase::SelfTest passed");
#endif
}
} // namespace Arcbit::Narrowphase
