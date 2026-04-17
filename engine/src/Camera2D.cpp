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
    // Invert the NDC transform: screen center → camera world position.
    // Divide by Zoom to account for the zoomed effective viewport.
    const Vec2 eff = GetEffectivePosition();
    return {
        eff.X + (screenPos.X - viewportSize.X * 0.5f) / Zoom,
        eff.Y + (screenPos.Y - viewportSize.Y * 0.5f) / Zoom,
    };
}

Vec2 Camera2D::WorldToScreen(const Vec2 worldPos, const Vec2 viewportSize) const
{
    const Vec2 eff = GetEffectivePosition();
    return {
        (worldPos.X - eff.X) * Zoom + viewportSize.X * 0.5f,
        (worldPos.Y - eff.Y) * Zoom + viewportSize.Y * 0.5f,
    };
}

} // namespace Arcbit
