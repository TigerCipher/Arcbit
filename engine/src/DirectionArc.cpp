#include <arcbit/physics/DirectionArc.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

#include <cmath>

namespace Arcbit
{
namespace
{
    // RadToDeg / Pi etc. live in core/Math.h.
    constexpr f32 FullTurn = 360.0f;
    constexpr f32 HalfTurn = 180.0f;

    // Wrap an angular delta into [-180, 180] so the abs() comparison against
    // an arc's half-width handles the 180/-180 boundary cleanly.
    inline f32 WrapDelta(f32 delta) noexcept
    {
        if (delta > HalfTurn) delta  -= FullTurn;
        if (delta < -HalfTurn) delta += FullTurn;
        return delta;
    }
} // anonymous namespace

bool IsContactBlocked(const std::vector<DirectionArc>& arcs,
                      const Vec2                       contactDirWorld,
                      const f32                        obstacleRotation) noexcept
{
    // Default convention: empty list = block all directions.
    if (arcs.empty()) return true;

    // Fast path — any single full-coverage arc trivially blocks.
    for (const auto& arc : arcs)
        if (arc.IsFullCoverage()) return true;

    // Convert the contact direction to the obstacle's local frame. Rotation
    // is the world-space rotation of the obstacle; rotating the contact by
    // -rotation expresses it in the local frame the arcs are defined in.
    f32 localX = contactDirWorld.X;
    f32 localY = contactDirWorld.Y;
    if (obstacleRotation != 0.0f) {
        const f32 c = std::cos(-obstacleRotation);
        const f32 s = std::sin(-obstacleRotation);
        localX = c * contactDirWorld.X - s * contactDirWorld.Y;
        localY = s * contactDirWorld.X + c * contactDirWorld.Y;
    }

    // Convert to degrees in [-180, 180]. atan2 returns radians in [-π, π].
    const f32 angleDeg = std::atan2(localY, localX) * RadToDeg;

    // Strict `<`: a contact exactly on the half-plane boundary (e.g. pure
    // east/west on a NorthOnly halfwidth-90° arc) is NOT inside the arc.
    // That matches "only the north face blocks" — a perpendicular contact is
    // east or west, not north — and is what tile-aligned movement needs so
    // sliding past a directional tile from the side isn't falsely blocked.
    for (const auto& arc : arcs) {
        const f32 delta = WrapDelta(angleDeg - arc.CenterDegrees);
        if (std::abs(delta) < arc.HalfWidthDegrees) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// SelfTest — preset shapes + arc check covering the boundary cases.
// ---------------------------------------------------------------------------

void DirectionArcSelfTest()
{
    #ifdef ARCBIT_DEBUG
    // Cardinal unit vectors (screen coords: +Y is south).
    constexpr Vec2 east  = {1.0f, 0.0f};
    constexpr Vec2 west  = {-1.0f, 0.0f};
    constexpr Vec2 north = {0.0f, -1.0f};
    constexpr Vec2 south = {0.0f, 1.0f};

    // Empty list → block-all default.
    ARCBIT_ASSERT(IsContactBlocked({}, north, 0.0f),
                  "DirectionArc: empty list must block (default convention)");
    ARCBIT_ASSERT(IsContactBlocked({}, east, 0.0f),
                  "DirectionArc: empty list must block (default convention)");

    // AllDirections() / IsFullCoverage fast-path.
    ARCBIT_ASSERT(IsContactBlocked(DirectionArc::AllDirections(), east, 0.0f),
                  "DirectionArc: AllDirections preset must block any direction");

    // Vertical(): blocks N/S, lets E/W pass.
    {
        const auto v = DirectionArc::Vertical();
        ARCBIT_ASSERT(IsContactBlocked(v, north, 0.0f), "Vertical: should block north");
        ARCBIT_ASSERT(IsContactBlocked(v, south, 0.0f), "Vertical: should block south");
        ARCBIT_ASSERT(!IsContactBlocked(v, east, 0.0f), "Vertical: should pass east");
        ARCBIT_ASSERT(!IsContactBlocked(v, west, 0.0f), "Vertical: should pass west");
    }

    // Horizontal(): blocks E/W, lets N/S pass.
    {
        const auto h = DirectionArc::Horizontal();
        ARCBIT_ASSERT(IsContactBlocked(h, east, 0.0f), "Horizontal: should block east");
        ARCBIT_ASSERT(IsContactBlocked(h, west, 0.0f), "Horizontal: should block west");
        ARCBIT_ASSERT(!IsContactBlocked(h, north, 0.0f), "Horizontal: should pass north");
        ARCBIT_ASSERT(!IsContactBlocked(h, south, 0.0f), "Horizontal: should pass south");
    }

    // One-sided gates — NorthOnly blocks north, passes south. The default
    // half-width of 90° puts east/west exactly on the arc boundary; with the
    // strict `<` comparison the boundary is *outside* the arc, so a pure E/W
    // contact passes (matches "only the north face blocks").
    {
        const auto wide   = DirectionArc::NorthOnly();      // halfWidth 90
        const auto narrow = DirectionArc::NorthOnly(45.0f); // ±45° around north
        ARCBIT_ASSERT(IsContactBlocked(wide, north, 0.0f), "NorthOnly: should block north");
        ARCBIT_ASSERT(!IsContactBlocked(wide, south, 0.0f), "NorthOnly: should pass south (180° away)");
        ARCBIT_ASSERT(!IsContactBlocked(wide, east, 0.0f),
                      "NorthOnly(90): east is on the boundary → passes with strict `<`");
        ARCBIT_ASSERT(!IsContactBlocked(wide, west, 0.0f),
                      "NorthOnly(90): west is on the boundary → passes with strict `<`");
        ARCBIT_ASSERT(!IsContactBlocked(narrow, east, 0.0f),
                      "NorthOnly(45): east is well outside the narrow arc → passes");
        ARCBIT_ASSERT(!IsContactBlocked(narrow, west, 0.0f),
                      "NorthOnly(45): west is well outside the narrow arc → passes");
    }

    // Off-axis approach — Vertical() defaults to ±60°, so 30° off vertical
    // is inside the arc; 75° off vertical (i.e. ~north-east) is outside.
    {
        const auto v = DirectionArc::Vertical();
        // 30° off north toward east: angle = -90° + 30° = -60° (still inside ±60°).
        const f32 r30 = -60.0f / RadToDeg;
        const Vec2 d30 = {std::cos(r30), std::sin(r30)};
        ARCBIT_ASSERT(IsContactBlocked(v, d30, 0.0f),
                      "Vertical: 30° off vertical should still block (inside ±60° arc)");

        // 75° off north toward east: angle = -90° + 75° = -15° → outside ±60°.
        const f32 r75 = -15.0f / RadToDeg;
        const Vec2 d75 = {std::cos(r75), std::sin(r75)};
        ARCBIT_ASSERT(!IsContactBlocked(v, d75, 0.0f),
                      "Vertical: 75° off vertical should pass (outside ±60° arc)");
    }

    // Rotated obstacle — local "vertical" arcs rotated 90° clockwise become
    // "horizontal" in world frame.
    {
        const auto      v        = DirectionArc::Vertical();
        constexpr f32   HalfPi   = 1.5707963267948966f;
        // Local frame is rotated +90° clockwise in screen coords. A world-east
        // contact direction maps to local-north, which is inside the Vertical arc.
        ARCBIT_ASSERT(IsContactBlocked(v, east, HalfPi),
                      "Vertical@90°: rotated arcs should block world-east contacts");
        // World-north contact maps to local-east, which is *outside* the Vertical arc.
        ARCBIT_ASSERT(!IsContactBlocked(v, north, HalfPi),
                      "Vertical@90°: rotated arcs should pass world-north contacts");
    }

    // Wrap boundary — an arc centered at 180° must accept contacts at -180°.
    {
        const std::vector<DirectionArc> west180{{180.0f, 30.0f}};
        ARCBIT_ASSERT(IsContactBlocked(west180, west, 0.0f),
                      "DirectionArc: arc centered at 180 must wrap to accept -180 contacts");
    }

    LOG_DEBUG(Engine, "DirectionArc::SelfTest passed");
    #endif
}
} // namespace Arcbit
