#include <arcbit/ecs/World.h>
#include <arcbit/ecs/AnimatorStateMachine.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/assets/SpriteSheet.h>
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
        return packet.ReferenceSize.X > 0.0f
               ? packet.ReferenceSize
               : Vec2{static_cast<f32>(packet.Width), static_cast<f32>(packet.Height)};
    }

    // ---------------------------------------------------------------------------
    // Built-in system implementations
    // ---------------------------------------------------------------------------

    void RegisterLifetimeSystem(World& world)
    {
        world.RegisterSystem("Lifetime", [](Scene& scene, const f32 dt) {
            std::vector<Entity> toDestroy;

            scene.GetWorld()
                 .Query<Lifetime>()
                 .Without<Disabled>()
                 .ForEach([&](const Entity e, Lifetime& lt) {
                     lt.Remaining -= dt;
                     if (lt.Remaining <= 0.0f)
                         toDestroy.push_back(e);
                 });

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
                        // Pivot offset: shifts the rendered quad so the sprite's
                        // logical anchor aligns with Transform2D.Position.
                        // Pivot {0.5,0.5} = centre (no offset); {0.5,1.0} = bottom-centre.
                        pos.X += (0.5f - sr.Pivot.X) * t.Scale.X;
                        pos.Y += (0.5f - sr.Pivot.Y) * t.Scale.Y;
                        if (!cam.IsVisible(pos, t.Scale, ref))
                            return;

                        Sprite s{};
                        s.Texture  = sr.Texture;
                        s.Sampler  = sr.Sampler;
                        s.Position = pos;
                        s.Size     = t.Scale;
                        s.UV       = sr.FlipX
                                         ? UVRect{ sr.UV.U1, sr.UV.V0, sr.UV.U0, sr.UV.V1 }
                                         : sr.UV;
                        s.Tint     = sr.Tint;
                        s.Layer    = sr.Layer;
                        packet.Sprites.push_back(s);
                    });
            // clang-format on
        });
    }

    void RegisterAnimatorStateMachineSystem(World& world)
    {
        world.RegisterSystem("AnimatorStateMachine", [](Scene& scene, const f32 /*dt*/) {
        // clang-format off
        scene.GetWorld()
                .Query<AnimatorStateMachine, Animator>()
                .Without<Disabled>()
                .ForEach([](AnimatorStateMachine& sm, Animator& anim) {
                    if (!sm.Sheet || sm.CurrentStateName().empty())
                        return;

                    const auto* state = sm.CurrentState();
                    if (!state)
                        return;

                    // Compute clip progress fraction for exit-time checks.
                    f32 progress = 0.0f;
                    if (anim.Clip && !anim.Clip->Frames.empty())
                    {
                        const u32 count = static_cast<u32>(anim.Clip->Frames.size());
                        progress = anim.Finished
                                       ? 1.0f
                                       : static_cast<f32>(anim.FrameIndex) / static_cast<f32>(count);
                    }

                    const std::string next = sm.EvaluateTransitions(progress);
                    if (next.empty())
                        return;

                    sm.TransitionTo(next);
                    const auto* newState = sm.CurrentState();
                    if (!newState)
                        return;

                    anim.Clip       = sm.Sheet->GetAnimation(newState->ClipName);
                    anim.FrameIndex = 0;
                    anim.Elapsed    = 0.0f;
                    anim.Finished   = false;
                });
            // clang-format on
        });
    }

    void RegisterAnimatorSystem(World& world)
    {
        world.RegisterSystem("Animator", [](Scene& scene, const f32 dt) {
        // clang-format off
        scene.GetWorld()
                .Query<Animator, SpriteRenderer>()
                .Without<Disabled>()
                .ForEach([dt](Animator& anim, SpriteRenderer& sr) {
                    if (!anim.Playing || !anim.Clip || !anim.Sheet || anim.Clip->Frames.empty())
                        return;

                    anim.Elapsed += dt;

                    const f32 frameSecs = anim.Clip->Frames[anim.FrameIndex].DurationMs / 1000.0f;
                    if (anim.Elapsed >= frameSecs)
                    {
                        anim.Elapsed -= frameSecs;
                        const u32 count    = static_cast<u32>(anim.Clip->Frames.size());
                        const u32 prevFrame = anim.FrameIndex;
                        ++anim.FrameIndex;

                        if (anim.FrameIndex >= count)
                        {
                            if (anim.Clip->Loop)
                                anim.FrameIndex = 0;
                            else
                            {
                                anim.FrameIndex = count - 1;
                                anim.Finished   = true;
                            }
                        }

                        // Fire frame events when the frame index actually changes.
                        if (anim.FrameIndex != prevFrame && anim.OnEvent)
                            for (const auto& ev : anim.Clip->Frames[anim.FrameIndex].Events)
                                anim.OnEvent(ev);
                    }

                    const std::string& frameName = anim.Clip->Frames[anim.FrameIndex].FrameName;
                    if (const auto frame = anim.Sheet->GetFrame(frameName))
                    {
                        sr.UV    = frame->UV;
                        sr.Pivot = frame->Pivot;
                    }
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
    RegisterAnimatorStateMachineSystem(world); // evaluates transitions, updates Animator clip
    RegisterAnimatorSystem(world);             // advances frames, fires events, writes UV

    // Render-collect phase.
    RegisterSpriteRenderSystem(world);
    RegisterLightRenderSystem(world);
}
} // namespace Arcbit
