#include <arcbit/render/Camera2D.h>

#include <algorithm>
#include <cmath>

namespace Arcbit {

void Camera2D::Update(const f32 dt)
{
    _trauma = std::max(0.0f, _trauma - TraumaDecayRate * dt);
    _shakeTime += dt;

    // Shake magnitude scales with trauma² — small trauma = subtle shake.
    const f32 magnitude = _trauma * _trauma * MaxShakePixels;
    _shakeOffset = {
        magnitude * std::sin(_shakeTime * ShakeFrequency * 2.0f),
        magnitude * std::cos(_shakeTime * ShakeFrequency * 1.7f),
    };
}

void Camera2D::Follow(const Vec2 target, const f32 smoothing, const f32 dt)
{
    // Exponential decay: frame-rate independent, feels the same at any dt.
    const f32 t  = 1.0f - std::exp(-smoothing * dt);
    Position.X  += (target.X - Position.X) * t;
    Position.Y  += (target.Y - Position.Y) * t;
}

void Camera2D::ClampToBounds(const Vec2 worldMin, const Vec2 worldMax, const Vec2 viewportSize)
{
    // Half-extents of the visible area in world pixels at the current zoom.
    const f32 halfW = (viewportSize.X / Zoom) * 0.5f;
    const f32 halfH = (viewportSize.Y / Zoom) * 0.5f;

    // Only clamp each axis if the world is larger than the viewport along that axis.
    // If the world is smaller the camera stays centered rather than snapping to an edge.
    if (worldMax.X - worldMin.X > viewportSize.X / Zoom)
        Position.X = std::clamp(Position.X, worldMin.X + halfW, worldMax.X - halfW);

    if (worldMax.Y - worldMin.Y > viewportSize.Y / Zoom)
        Position.Y = std::clamp(Position.Y, worldMin.Y + halfH, worldMax.Y - halfH);
}

void Camera2D::AddTrauma(const f32 amount)
{
    _trauma = std::min(1.0f, _trauma + amount);
}

Vec2 Camera2D::GetEffectivePosition() const
{
    return { Position.X + _shakeOffset.X, Position.Y + _shakeOffset.Y };
}

Vec2 Camera2D::ScreenToWorld(const Vec2 screenPos, const Vec2 viewportSize) const
{
    // Un-project: screen → centered NDC → rotate back → world.
    const Vec2 eff = GetEffectivePosition();
    const f32 cx = (screenPos.X - viewportSize.X * 0.5f) / Zoom;
    const f32 cy = (screenPos.Y - viewportSize.Y * 0.5f) / Zoom;

    // Apply inverse camera rotation (rotate by +Rotation to undo -Rotation view transform).
    const f32 cosR = std::cos(Rotation);
    const f32 sinR = std::sin(Rotation);
    return {
        eff.X + cosR * cx - sinR * cy,
        eff.Y + sinR * cx + cosR * cy,
    };
}

Vec2 Camera2D::WorldToScreen(const Vec2 worldPos, const Vec2 viewportSize) const
{
    const Vec2 eff = GetEffectivePosition();
    const f32 dx = worldPos.X - eff.X;
    const f32 dy = worldPos.Y - eff.Y;

    // Apply camera rotation (rotate delta by -Rotation).
    const f32 cosR = std::cos(Rotation);
    const f32 sinR = std::sin(Rotation);
    const f32 rx =  cosR * dx + sinR * dy;
    const f32 ry = -sinR * dx + cosR * dy;

    return {
        rx * Zoom + viewportSize.X * 0.5f,
        ry * Zoom + viewportSize.Y * 0.5f,
    };
}

} // namespace Arcbit
