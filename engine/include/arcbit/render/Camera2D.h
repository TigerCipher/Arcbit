#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Camera2D
//
// Tracks world-space viewport position, zoom, rotation, and screen shake.
// Apply to a FramePacket each frame via:
//   packet.CameraPosition = camera.GetEffectivePosition();
//   packet.CameraZoom     = camera.Zoom;
//   packet.CameraRotation = camera.Rotation;
//
// Coordinate system
// -----------------
// Positions are in world-space pixels, matching Sprite::Position.
// CameraPosition maps to screen center; Y+ is downward (screen convention).
// Rotation is in radians, counter-clockwise positive.
// ---------------------------------------------------------------------------
class Camera2D
{
public:
    Vec2 Position = {};
    f32  Zoom     = 1.0f;
    f32  Rotation = 0.0f; // radians; world rotates opposite direction to camera

    // Call once per update tick. Decays trauma and recomputes the shake offset.
    void Update(f32 dt);

    // Frame-rate-independent smooth move toward a world-space target.
    // smoothing: higher = faster catch-up. Typical range: 3 (lazy) – 12 (snappy).
    void Follow(Vec2 target, f32 smoothing, f32 dt);

    // Clamps Position so the visible viewport stays fully inside [worldMin, worldMax].
    // Call after Follow() each tick. Does nothing if the world is smaller than the
    // viewport (no valid clamp range), so safe to call unconditionally.
    void ClampToBounds(Vec2 worldMin, Vec2 worldMax, Vec2 viewportSize);

    // Add screen-shake trauma [0, 1]. Values accumulate and are clamped to 1.
    // Trauma decays automatically in Update(). Shake magnitude = trauma².
    void AddTrauma(f32 amount);

    // Returns Position + current shake offset — use this for FramePacket.
    [[nodiscard]] Vec2 GetEffectivePosition() const;

    // Convert screen-space pixels (top-left origin) to world-space pixels.
    [[nodiscard]] Vec2 ScreenToWorld(Vec2 screenPos, Vec2 viewportSize) const;

    // Convert world-space pixels to screen-space pixels (top-left origin).
    [[nodiscard]] Vec2 WorldToScreen(Vec2 worldPos, Vec2 viewportSize) const;

    // Returns true if a world-space axis-aligned rect is at least partially
    // inside the camera's visible area. Safe to call with any rotation — the
    // test uses the axis-aligned bounding box of the rotated viewport, which
    // never produces false negatives (visible sprites are never culled).
    [[nodiscard]] bool IsVisible(Vec2 spritePos, Vec2 spriteSize, Vec2 viewportSize) const;

    // Returns true if a point light's circle overlaps the camera's visible area.
    // Uses a circle-vs-AABB test: a light is culled only if its nearest edge is
    // farther from the viewport than the light's radius.
    [[nodiscard]] bool IsLightVisible(Vec2 lightPos, f32 lightRadius, Vec2 viewportSize) const;

private:
    f32  _trauma      = 0.0f;
    Vec2 _shakeOffset = {};
    f32  _shakeTime   = 0.0f;

    // Shake tuning knobs.
    static constexpr f32 TraumaDecayRate = 1.5f;  // trauma units lost per second
    static constexpr f32 MaxShakePixels  = 20.0f; // maximum shake offset at trauma = 1
    static constexpr f32 ShakeFrequency  = 30.0f; // oscillations per second
};

} // namespace Arcbit
