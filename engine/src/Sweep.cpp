#include <arcbit/physics/Sweep.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/physics/Narrowphase.h>
#include <arcbit/physics/PhysicsWorld.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Arcbit::Sweep
{
namespace
{
    constexpr f32 SweepEpsilon = 1e-6f;

    // Per-axis slab-method timing. Returns false when the mover slides past the
    // slab without ever entering it (no contact possible). On success, fills
    // tEnter/tExit; either may be infinite when v == 0 and the axis ranges
    // already overlap.
    bool AxisSlab(const f32 mMin, const f32 mMax,
                  const f32 sMin, const f32 sMax,
                  const f32 v,
                  f32&      tEnter, f32& tExit) noexcept
    {
        if (std::abs(v) < SweepEpsilon) {
            // Stationary on this axis — overlap decides "always open" vs "never enterable".
            if (mMax < sMin || mMin > sMax) return false;
            tEnter = -std::numeric_limits<f32>::infinity();
            tExit  = std::numeric_limits<f32>::infinity();
            return true;
        }
        if (v > 0.0f) {
            tEnter = (sMin - mMax) / v;
            tExit  = (sMax - mMin) / v;
        }
        else {
            tEnter = (sMax - mMin) / v;
            tExit  = (sMin - mMax) / v;
        }
        return true;
    }
} // anonymous namespace

Result MovingAABBvsAABB(const AABB& moving, const Vec2 delta,
                        const AABB& stationary) noexcept
{
    f32 tEnterX = 0, tExitX = 0;
    f32 tEnterY = 0, tExitY = 0;
    if (!AxisSlab(moving.Min.X, moving.Max.X,
                  stationary.Min.X, stationary.Max.X,
                  delta.X, tEnterX, tExitX))
        return {};
    if (!AxisSlab(moving.Min.Y, moving.Max.Y,
                  stationary.Min.Y, stationary.Max.Y,
                  delta.Y, tEnterY, tExitY))
        return {};

    const f32 tEnter = std::max(tEnterX, tEnterY);
    const f32 tExit  = std::min(tExitX, tExitY);

    // No intersection in [0, 1]:
    //   tEnter > tExit : slabs miss in time
    //   tEnter > 1     : contact happens past the supplied delta
    //   tExit  < 0     : contact already happened in the past
    if (tEnter > tExit || tEnter > 1.0f || tExit < 0.0f) return {};

    Result r{};
    r.Hit = true;
    r.ToI = std::max(tEnter, 0.0f);

    // The axis with the later entry time decides which face was hit; normal
    // points opposite to the mover's motion on that axis.
    if (tEnterX > tEnterY)
        r.Normal = {delta.X > 0.0f ? -1.0f : 1.0f, 0.0f};
    else
        r.Normal = {0.0f, delta.Y > 0.0f ? -1.0f : 1.0f};
    return r;
}

Result Substep(const Collider2D& mover, const Vec2 origin, const Vec2 delta,
               const Collider2D& other, const Vec2 otherPos,
               const u32         substeps) noexcept
{
    if (substeps == 0) return {};

    // If the mover already overlaps the obstacle at t=0, the obstacle-center
    // → mover-position vector (the normal we'd compute below) is essentially
    // noise — the mover could be anywhere inside the obstacle. Match the
    // slab method's convention instead: report a normal pointing back along
    // the motion direction (the axis-equivalent of "the face the mover is
    // moving toward"). Without this, a circle mover inside a directional
    // box collider (e.g. walked through a Vertical-arc tree from the side)
    // would get spurious off-axis normals that fall inside the arc and trap
    // it; with this fix, the arc check sees the actual motion direction.
    if (Narrowphase::Overlap(mover, origin, other, otherPos)) {
        Result r{};
        r.Hit       = true;
        r.ToI       = 0.0f;
        const f32 dl = std::sqrt(delta.X * delta.X + delta.Y * delta.Y);
        r.Normal    = dl > SweepEpsilon
                          ? Vec2{-delta.X / dl, -delta.Y / dl}
                          : Vec2{1.0f, 0.0f};
        return r;
    }

    // Walk the delta in N equal steps. The first step where the boolean
    // narrowphase fires gives ToI = (k-1)/N — the position just before contact.
    for (u32 k = 1; k <= substeps; ++k) {
        const f32  t       = static_cast<f32>(k) / static_cast<f32>(substeps);
        const Vec2 testPos = {origin.X + delta.X * t, origin.Y + delta.Y * t};
        if (!Narrowphase::Overlap(mover, testPos, other, otherPos)) continue;

        Result r{};
        r.Hit = true;
        r.ToI = static_cast<f32>(k - 1) / static_cast<f32>(substeps);

        // Approximate normal: from the obstacle's center toward the mover's
        // last clear position. Good enough for sub-step circle resolution; a
        // proper closed-form circle sweep (future) would compute the exact
        // contact point.
        const Vec2 lastClear   = {origin.X + delta.X * r.ToI, origin.Y + delta.Y * r.ToI};
        const Vec2 otherCenter = {otherPos.X + other.Offset.X, otherPos.Y + other.Offset.Y};
        const Vec2 moverCenter = {lastClear.X + mover.Offset.X, lastClear.Y + mover.Offset.Y};

        Vec2 n = {moverCenter.X - otherCenter.X, moverCenter.Y - otherCenter.Y};
        if (const f32 len = std::sqrt(n.X * n.X + n.Y * n.Y); len > SweepEpsilon) {
            n.X /= len;
            n.Y /= len;
        }
        else {
            // Degenerate: mover sits on top of an obstacle. Use the opposite of motion.
            const f32 dl = std::sqrt(delta.X * delta.X + delta.Y * delta.Y);

            n = dl > SweepEpsilon
                ? Vec2{-delta.X / dl, -delta.Y / dl}
                : Vec2{1.0f, 0.0f};
        }
        r.Normal = n;
        return r;
    }
    return {};
}

Result SweepAgainst(const Collider2D& mover, const Vec2 origin, const Vec2 delta,
                    const Collider2D& other, const Vec2 otherPos) noexcept
{
    const bool axisAlignedBoxA = (mover.Shape == ColliderShape::Box && mover.Rotation == 0.0f);
    const bool axisAlignedBoxB = (other.Shape == ColliderShape::Box && other.Rotation == 0.0f);

    if (axisAlignedBoxA && axisAlignedBoxB) {
        // Both axis-aligned boxes — slab method is exact and cheapest.
        const AABB moverAABB = PhysicsWorld::ComputeWorldAABB(mover, origin);
        const AABB otherAABB = PhysicsWorld::ComputeWorldAABB(other, otherPos);
        return MovingAABBvsAABB(moverAABB, delta, otherAABB);
    }

    // Anything involving a circle (or rotated box, until 22E) — sub-step.
    return Substep(mover, origin, delta, other, otherPos, 8u);
}

// ---------------------------------------------------------------------------
// SelfTest — slab-method correctness on hand-picked cases.
// ---------------------------------------------------------------------------

void SelfTest()
{
    #ifdef ARCBIT_DEBUG
    constexpr f32 tol = 0.001f;

    // Head-on X: closing distance 20, delta.x 40 -> ToI = 0.5, normal = (-1, 0).
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({30.0f, 0.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {40.0f, 0.0f}, stationary);
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: head-on X miss");
        ARCBIT_ASSERT(std::abs(r.ToI - 0.5f) < tol, "Sweep::SelfTest: head-on X ToI wrong");
        ARCBIT_ASSERT(r.Normal.X < 0.0f && std::abs(r.Normal.Y) < tol,
                      "Sweep::SelfTest: head-on X normal wrong");
    }

    // No-contact: stationary box well above the mover's path.
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({30.0f, 100.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {40.0f, 0.0f}, stationary);
        ARCBIT_ASSERT(!r.Hit, "Sweep::SelfTest: false positive on miss");
        ARCBIT_ASSERT(r.ToI == 1.0f, "Sweep::SelfTest: ToI should be 1.0 on miss");
    }

    // Tangent at start: edges already touching. Convention: counts as Hit at ToI=0.
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({10.0f, 0.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {1.0f, 0.0f}, stationary);
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: tangent contact missed");
        ARCBIT_ASSERT(r.ToI == 0.0f, "Sweep::SelfTest: tangent contact should be ToI=0");
    }

    // Glancing: mover travels along X but stationary is offset on Y → no overlap.
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({30.0f, 12.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {40.0f, 0.0f}, stationary);
        ARCBIT_ASSERT(!r.Hit, "Sweep::SelfTest: glancing horizontal pass should miss");
    }

    // Head-on Y selecting Y normal: closing 20 down, delta.y -40 -> ToI 0.5, normal (0, +1).
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({0.0f, -30.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {0.0f, -40.0f}, stationary);
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: head-on Y miss");
        ARCBIT_ASSERT(std::abs(r.ToI - 0.5f) < tol, "Sweep::SelfTest: head-on Y ToI wrong");
        ARCBIT_ASSERT(r.Normal.Y > 0.0f && std::abs(r.Normal.X) < tol,
                      "Sweep::SelfTest: head-on Y normal wrong");
    }

    // Diagonal hit where X-entry > Y-entry: corner approach selects X normal.
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({30.0f, 12.0f}, {5.0f, 5.0f});
        // Move {40, 20}. X needs to travel 20 to enter (t=0.5); Y needs 2 to enter (t=0.1).
        // tEnterX > tEnterY → X normal selected.
        const Result r = MovingAABBvsAABB(mover, {40.0f, 20.0f}, stationary);
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: diagonal hit missed");
        ARCBIT_ASSERT(std::abs(r.ToI - 0.5f) < tol, "Sweep::SelfTest: diagonal ToI wrong");
        ARCBIT_ASSERT(r.Normal.X < 0.0f && std::abs(r.Normal.Y) < tol,
                      "Sweep::SelfTest: diagonal normal axis wrong");
    }

    // Sub-step circle vs box: circle moving rightward into a box should report
    // a hit with the normal pointing back toward the mover.
    {
        const Collider2D circle{
            .Shape  = ColliderShape::Circle,
            .Radius = 4.0f,
        };
        const Collider2D box{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {5.0f, 5.0f},
        };
        const Result r = SweepAgainst(circle, {0.0f, 0.0f}, {40.0f, 0.0f},
                                      box, {30.0f, 0.0f});
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: substep circle-box missed contact");
        ARCBIT_ASSERT(r.ToI < 1.0f, "Sweep::SelfTest: substep circle-box ToI should be < 1");
        ARCBIT_ASSERT(r.Normal.X < 0.0f, "Sweep::SelfTest: substep circle-box normal direction wrong");
    }

    LOG_DEBUG(Engine, "Sweep::SelfTest passed");
    #endif
}
} // namespace Arcbit::Sweep
