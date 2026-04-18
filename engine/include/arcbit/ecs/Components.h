#pragma once

#include <arcbit/assets/AssetTypes.h>
#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>

#include <string>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Required components — every entity carries both.
// ---------------------------------------------------------------------------

// Single source of truth for where an entity is in the world.
// Scale is used as world-space pixel size by SpriteRenderSystem (e.g. {32, 32}).
struct Transform2D
{
    Vec2 Position = {};
    Vec2 Scale    = { 1.0f, 1.0f };
    f32  Rotation = 0.0f;             // radians, clockwise
};

// Coarse string classification. Use marker components for per-frame filtering;
// Tag is for setup queries and editor display.
struct Tag
{
    std::string Value = "Entity";
};

// ---------------------------------------------------------------------------
// Optional identity components
// ---------------------------------------------------------------------------

// Human-readable name. Add only for entities with game-design identity
// (player, named NPCs, quest objects). Not required — terrain and pooled
// enemies don't need names.
struct Name
{
    std::string Value;
};

// ---------------------------------------------------------------------------
// Render components
// ---------------------------------------------------------------------------

// Visual representation. SpriteRenderSystem reads (Transform2D, SpriteRenderer)
// and pushes a Sprite into FramePacket. Transform2D.Scale is the world-space size.
struct SpriteRenderer
{
    TextureHandle Texture;
    SamplerHandle Sampler;
    UVRect        UV    = { 0.0f, 0.0f, 1.0f, 1.0f };
    Color         Tint  = Color::White();
    i32           Layer = 0;
};

// Light properties. LightRenderSystem reads (Transform2D, LightEmitter)
// and pushes a PointLight into FramePacket.
struct LightEmitter
{
    f32   Radius     = 200.0f;
    f32   Intensity  = 1.0f;
    Color LightColor = Color::NaturalLight();
};

// ---------------------------------------------------------------------------
// Behavior components
// ---------------------------------------------------------------------------

// Scroll rate multiplier for parallax layers.
// SpriteRenderSystem applies: effectivePos = pos + camPos * (1 - scrollFactor).
//   0.0 = fixed (sky, overlay), 0.5 = mid-ground, 1.0 = normal world layer.
struct Parallax
{
    Vec2 ScrollFactor = { 0.5f, 0.5f };
};

// Marks the entity the scene camera should follow. If multiple entities carry
// this, CameraFollowSystem picks the first one it finds.
struct CameraTarget
{
    f32 Lag = 0.1f;  // 0.0 = instant snap, 1.0 = no follow
};

// Empty marker — all built-in systems use .Without<Disabled>() so disabled
// entities are invisible to them. Useful for pooling and off-screen NPCs.
struct Disabled {};

// Auto-destroys the entity after Remaining seconds reach zero.
// Intended for projectiles, particles, and temporary effects.
struct Lifetime
{
    f32 Remaining = 0.0f;
};

} // namespace Arcbit
