#include <arcbit/ecs/World.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Log.h>

#include <cmath>
#include <vector>

namespace Arcbit
{

namespace
{
// ---------------------------------------------------------------------------
// Built-in system helpers
// ---------------------------------------------------------------------------

Vec2 GetRefSize(const FramePacket& packet)
{
    return packet.ReferenceSize.X > 0.0f ? packet.ReferenceSize
                                         : Vec2{ static_cast<f32>(packet.Width), static_cast<f32>(packet.Height) };
}

// ---------------------------------------------------------------------------
// Built-in system implementations
// ---------------------------------------------------------------------------

void RegisterLifetimeSystem(World& world)
{
    world.RegisterSystem("Lifetime", [](Scene& scene, const f32 dt) {
        std::vector<Entity> toDestroy;
        // clang-format off
        scene.GetWorld()
                .Query<Lifetime>()
                .Without<Disabled>()
                .ForEach([&](const Entity e, Lifetime& lt) {
                    lt.Remaining -= dt;
                    if (lt.Remaining <= 0.0f)
                        toDestroy.push_back(e);
                });
        // clang-format on

        for (const Entity e : toDestroy)
            scene.GetWorld().DestroyEntity(e);
    });
}

void RegisterFreeMovementSystem(World& world)
{
    world.RegisterSystem("FreeMovement", [](Scene& scene, f32 dt) {
        // clang-format off
        scene.GetWorld()
                .Query<Transform2D, FreeMovement>()
                .Without<Disabled>()
                .ForEach([dt](Transform2D& t, FreeMovement& fm) {
                    if (fm.MaxSpeed > 0.0f)
                    {
                        const f32 sqLen = fm.Velocity.X * fm.Velocity.X + fm.Velocity.Y * fm.Velocity.Y;
                        if (sqLen > fm.MaxSpeed * fm.MaxSpeed)
                        {
                            const f32 inv = fm.MaxSpeed / std::sqrt(sqLen);
                            fm.Velocity   = fm.Velocity * inv;
                        }
                    }
                    t.Position  = t.Position + fm.Velocity * dt;
                    fm.Velocity = fm.Velocity * std::exp(-fm.Friction * dt);
                });
        // clang-format on

    });
}

void RegisterCameraFollowSystem(World& world)
{
    world.RegisterSystem("CameraFollow", [](Scene& scene, const f32 dt) {
        // clang-format off
        scene.GetWorld()
                .Query<const Transform2D, const CameraTarget>()
                .Without<Disabled>()
                .ForEach(
                    [&](const Transform2D& t, const CameraTarget& ct) {
                        const f32 smoothing = ct.Lag < 1.0f ? 10.0f * (1.0f - ct.Lag) : 0.001f;
                        scene.GetCamera().Follow(t.Position, smoothing, dt);
                    });
        // clang-format on
    });
}

void RegisterSpriteRenderSystem(World& world)
{
    world.RegisterRenderSystem("SpriteRender", [](Scene& scene, FramePacket& packet) {
        const Camera2D& cam    = scene.GetCamera();
        const Vec2      camPos = cam.GetEffectivePosition();
        const Vec2      ref    = GetRefSize(packet);

        // clang-format off
        scene.GetWorld()
                .Query<const Transform2D, const SpriteRenderer>()
                .Without<Disabled>()
                .ForEach(
                    [&](const Entity e, const Transform2D& t, const SpriteRenderer& sr) {
                        Vec2 pos = t.Position;
                        if (const auto* p = scene.GetWorld().GetComponent<Parallax>(e))
                        {
                            pos.X += camPos.X * (1.0f - p->ScrollFactor.X);
                            pos.Y += camPos.Y * (1.0f - p->ScrollFactor.Y);
                        }
                        if (!cam.IsVisible(pos, t.Scale, ref))
                            return;

                        Sprite s{};
                        s.Texture  = sr.Texture;
                        s.Sampler  = sr.Sampler;
                        s.Position = pos;
                        s.Size     = t.Scale;
                        s.UV       = sr.UV;
                        s.Tint     = sr.Tint;
                        s.Layer    = sr.Layer;
                        packet.Sprites.push_back(s);
                    });
        // clang-format on
    });
}

void RegisterLightRenderSystem(World& world)
{
    world.RegisterRenderSystem("LightRender", [](Scene& scene, FramePacket& packet) {
        const Camera2D& cam = scene.GetCamera();
        const Vec2      ref = GetRefSize(packet);

        // clang-format off
        scene.GetWorld()
                .Query<const Transform2D, const LightEmitter>()
                .Without<Disabled>()
                .ForEach(
                    [&](const Transform2D& t, const LightEmitter& le) {
                        if (!cam.IsLightVisible(t.Position, le.Radius, ref))
                            return;

                        PointLight pl{};
                        pl.Position   = t.Position;
                        pl.Radius     = le.Radius;
                        pl.Intensity  = le.Intensity;
                        pl.LightColor = le.LightColor;
                        packet.Lights.push_back(pl);
                    });
        // clang-format on
    });
}
} // anonymous namespace


// ---------------------------------------------------------------------------
// Forward declaration — called from Scene.cpp
// ---------------------------------------------------------------------------
void RegisterBuiltinSystems(World& world);

// ---------------------------------------------------------------------------
// Registration entry point — called from Scene::Scene()
// ---------------------------------------------------------------------------

void RegisterBuiltinSystems(World& world)
{
    // Update phase — order matches the blueprint execution table.
    RegisterLifetimeSystem(world);
    RegisterFreeMovementSystem(world);
    RegisterCameraFollowSystem(world);

    // Render-collect phase.
    RegisterSpriteRenderSystem(world);
    RegisterLightRenderSystem(world);
}

} // namespace Arcbit
