#pragma once

#include <arcbit/core/Types.h>

#include <vector>

namespace Arcbit
{
// One angular arc, expressed in the collider's local frame. The arc covers the
// range [CenterDegrees - HalfWidthDegrees, CenterDegrees + HalfWidthDegrees].
//
// Convention: degrees, with 0 = +X (east), 90 = +Y (south), -90 = north.
// A list of arcs on a Collider2D's BlockedFrom field describes the directions
// from which approaches will be treated as blocking contacts. Approaches whose
// contact direction does not fall inside any arc pass through the collider.
//
// The narrowphase that consumes BlockedFrom lands in Phase 22D. Until then the
// field is carried but not consulted — every contact blocks regardless.
struct DirectionArc
{
    f32 CenterDegrees    = 0.0f;
    f32 HalfWidthDegrees = 180.0f; // 180 = full coverage (always block)

    // Field-wise equality; required for comparing BlockedFrom vectors when the
    // tile collider greedy-mesher decides whether two tiles can merge.
    bool operator==(const DirectionArc&) const = default;

    // Returns true when an arc spans the entire circle and never needs to be
    // tested in the narrowphase — useful as a fast-path check.
    [[nodiscard]] constexpr bool IsFullCoverage() const noexcept { return HalfWidthDegrees >= 180.0f; }

    // Preset: a single arc covering the full 360°. Equivalent to leaving
    // Collider2D::BlockedFrom empty (the cheaper default), but available if a
    // caller wants to be explicit, or as a base to mutate.
    [[nodiscard]] static std::vector<DirectionArc> AllDirections() { return {{0.0f, 180.0f}}; }

    // Additional presets (Vertical, Horizontal, NorthOnly, ...) land in
    // Phase 22D alongside the narrowphase that consumes them.
};
} // namespace Arcbit
