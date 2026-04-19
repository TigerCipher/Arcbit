#include <arcbit/ecs/World.h>
#include <arcbit/ecs/AnimatorStateMachine.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/tilemap/TileMap.h>
#include <arcbit/core/Log.h>

#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
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

    // -------------------------------------------------------------------------
    // Per-tile UV computation helpers
    // -------------------------------------------------------------------------

    UVRect ComputeTileUV(const TileAtlasEntry& entry, const u32 tileId,
                         const TileDef* def, const f32 elapsed,
                         const u32 tileX, const u32 tileY)
    {
        const u32 localId = tileId - entry.BaseId;
        const u32 cols    = entry.Atlas.Columns();

        if (def && !def->Animation.empty())
        {
            // Flip-book: position-based phase prevents lockstep marching.
            u32 totalMs = 0;
            for (const auto& f : def->Animation) totalMs += f.DurationMs;
            if (totalMs == 0) return entry.Atlas.GetUV(localId % cols, localId / cols);

            const u32 phaseMs = (tileX * 37u + tileY * 53u) % totalMs;
            u32 t     = (static_cast<u32>(elapsed * 1000.0f) + phaseMs) % totalMs;
            u32 accum = 0;
            for (const auto& f : def->Animation)
            {
                if (t < accum + f.DurationMs)
                    return entry.Atlas.GetUV(f.TileX, f.TileY);
                accum += f.DurationMs;
            }
            const auto& last = def->Animation.back();
            return entry.Atlas.GetUV(last.TileX, last.TileY);
        }

        // Static tile — use the ID's natural position in the atlas grid.
        UVRect uv = entry.Atlas.GetUV(localId % cols, localId / cols);

        if (def && (def->UVScroll.X != 0.0f || def->UVScroll.Y != 0.0f))
        {
            const f32 sx = std::fmod(elapsed * def->UVScroll.X, 1.0f);
            const f32 sy = std::fmod(elapsed * def->UVScroll.Y, 1.0f);
            uv.U0 += sx; uv.U1 += sx;
            uv.V0 += sy; uv.V1 += sy;
        }
        return uv;
    }

    void RegisterTilemapRenderSystem(World& world)
    {
        using Clock     = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        world.RegisterRenderSystem("TilemapRender",
            [startTime = Clock::now()](Scene& scene, FramePacket& packet) mutable
        {
            const TileMap& tileMap = scene.GetTileMap();
            if (tileMap.GetChunks().empty()) return;

            const Camera2D& cam     = scene.GetCamera();
            const Vec2      camPos  = cam.GetEffectivePosition();
            const Vec2      ref     = GetRefSize(packet);
            const f32       ts      = tileMap.GetTileSize();
            const f32       elapsed = std::chrono::duration<f32>(Clock::now() - startTime).count();

            // Chunk world half-size for coarse culling.
            const f32 chunkWorld = static_cast<f32>(ChunkSize) * ts;
            const Vec2 chunkHalf = { chunkWorld * 0.5f, chunkWorld * 0.5f };

            for (const auto& [key, chunk] : tileMap.GetChunks())
            {
                // Decode chunk coords from key.
                const i32 chunkX = static_cast<i32>(static_cast<u32>(key >> 32));
                const i32 chunkY = static_cast<i32>(static_cast<u32>(key & 0xFFFFFFFF));

                // Coarse chunk visibility: center of chunk in world space.
                const Vec2 chunkCenter = {
                    (static_cast<f32>(chunkX) * chunkWorld) + chunkWorld * 0.5f - ts * 0.5f,
                    (static_cast<f32>(chunkY) * chunkWorld) + chunkWorld * 0.5f - ts * 0.5f,
                };
                if (!cam.IsVisible(chunkCenter, { chunkWorld + ts * 2.0f, chunkWorld + ts * 2.0f }, ref))
                    continue;

                for (u32 layer = 0; layer < LayerCount; ++layer)
                {
                    for (u32 cell = 0; cell < ChunkSize * ChunkSize; ++cell)
                    {
                        const u32 tileId = chunk.Tiles[layer][cell];
                        if (tileId == 0) continue;

                        const TileAtlasEntry* entry = tileMap.FindAtlas(tileId);
                        if (!entry || !entry->Atlas.IsValid()) continue;

                        const u32 localX = cell % ChunkSize;
                        const u32 localY = cell / ChunkSize;
                        const i32 tileX  = chunkX * static_cast<i32>(ChunkSize) + static_cast<i32>(localX);
                        const i32 tileY  = chunkY * static_cast<i32>(ChunkSize) + static_cast<i32>(localY);

                        const Vec2 worldPos = tileMap.TileToWorld(tileX, tileY);
                        if (!cam.IsVisible(worldPos, { ts, ts }, ref)) continue;

                        const TileDef* def = tileMap.FindTileDef(tileId);
                        const UVRect   uv  = ComputeTileUV(*entry, tileId, def, elapsed,
                                                           static_cast<u32>(tileX),
                                                           static_cast<u32>(tileY));

                        // Layer sort: Ground = -1000, Objects = Y-sorted, Overlay = 1000000.
                        i32 sortLayer = -1000;
                        if (layer == 1)
                            sortLayer = static_cast<i32>(worldPos.Y + ts * 0.5f);
                        else if (layer == 2)
                            sortLayer = 1000000;

                        Sprite s{};
                        s.Texture  = entry->Atlas.GetTexture();
                        s.Sampler  = entry->Sampler;
                        s.Position = worldPos;
                        s.Size     = { ts, ts };
                        s.UV       = uv;
                        s.Layer    = sortLayer;
                        packet.Sprites.push_back(s);
                    }
                }
            }
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
                        s.Texture   = sr.Texture;
                        s.Sampler   = sr.Sampler;
                        s.Position  = pos;
                        s.Size      = t.Scale;
                        s.UV        = sr.FlipX
                                          ? UVRect{ sr.UV.U1, sr.UV.V0, sr.UV.U0, sr.UV.V1 }
                                          : sr.UV;
                        s.Tint      = sr.Tint;
                        s.Rotation  = t.Rotation;
                        s.Layer     = sr.Layer;
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

    void RegisterAudioSystem(World& world)
    {
        // Key: packed entity (Generation << 32 | Index) → opaque ma_sound handle.
        // The map is the only owner of destroyed entities' handles — once an entity
        // is gone from the query, this map is the last reference to its sound.
        using SoundMap = std::unordered_map<u64, void*>;

        world.RegisterSystem("Audio", [sounds = SoundMap{}](Scene& scene, f32 /*dt*/) mutable {
            // Move the listener with the camera so attenuation is camera-relative.
            AudioManager::SetListenerPosition(scene.GetCamera().GetEffectivePosition());

            std::unordered_set<u64> seen;

            scene.GetWorld()
                .Query<const Transform2D, AudioSource>()
                .Without<Disabled>()
                .ForEach([&](Entity e, const Transform2D& t, AudioSource& src) {
                    const u64 key = (static_cast<u64>(e.Generation) << 32) | e.Index;
                    seen.insert(key);

                    // Start a new sound the first time we see a playing source.
                    if (!src._handle && src.Playing)
                    {
                        src._handle = AudioManager::CreateSpatialSound(src.Path, src.Loop, src.Volume);
                        sounds[key] = src._handle;
                    }

                    if (src._handle)
                    {
                        AudioManager::SetSpatialSoundPosition(src._handle, t.Position, src.Radius);

                        // Playing set to false at runtime — stop and release.
                        if (!src.Playing)
                        {
                            AudioManager::DestroySpatialSound(src._handle);
                            src._handle = nullptr;
                            sounds.erase(key);
                        }
                    }
                });

            // Destroy sounds for entities that were removed since last tick.
            for (auto it = sounds.begin(); it != sounds.end(); )
            {
                if (!seen.count(it->first))
                {
                    AudioManager::DestroySpatialSound(it->second);
                    it = sounds.erase(it);
                }
                else { ++it; }
            }
        });
    }

    // ---------------------------------------------------------------------------
    // Shadow raycast helpers
    // ---------------------------------------------------------------------------

    // DDA grid traversal: returns distance from origin along unit-vector dir
    // to the nearest solid tile, or maxDist if none is found within range.
    f32 RayAABBDist(const Vec2 origin, const Vec2 dir, const f32 maxDist,
                    const TileMap& tileMap)
    {
        const f32 ts     = tileMap.GetTileSize();
        const f32 halfTs = ts * 0.5f;

        // TileToWorld returns tile centers (n*ts), so tile n visually spans
        // [n*ts - halfTs, n*ts + halfTs]. Shifting by +halfTs makes floor(x/ts)
        // align with those center-anchored boundaries.
        const f32 sx = origin.X + halfTs;
        const f32 sy = origin.Y + halfTs;

        i32 tileX = static_cast<i32>(std::floor(sx / ts));
        i32 tileY = static_cast<i32>(std::floor(sy / ts));

        const i32 stepX = dir.X >= 0.0f ? 1 : -1;
        const i32 stepY = dir.Y >= 0.0f ? 1 : -1;

        const f32 invX = dir.X != 0.0f ? ts / std::abs(dir.X) : 1e30f;
        const f32 invY = dir.Y != 0.0f ? ts / std::abs(dir.Y) : 1e30f;

        f32 tMaxX = dir.X != 0.0f
            ? (dir.X > 0.0f ? ((tileX + 1) * ts - sx) : (sx - tileX * ts)) / std::abs(dir.X)
            : 1e30f;
        f32 tMaxY = dir.Y != 0.0f
            ? (dir.Y > 0.0f ? ((tileY + 1) * ts - sy) : (sy - tileY * ts)) / std::abs(dir.Y)
            : 1e30f;

        f32 t = 0.0f;
        while (t < maxDist)
        {
            if (tileMap.BlocksLight(tileX, tileY))
                return t;

            if (tMaxX < tMaxY) { t = tMaxX; tMaxX += invX; tileX += stepX; }
            else                { t = tMaxY; tMaxY += invY; tileY += stepY; }
        }
        return maxDist;
    }

    // Casts ShadowResolution rays from origin, recording the distance to the
    // first solid tile at each polar angle bucket.
    ShadowMapData ComputeShadowMap(const u32 lightIndex, const Vec2 origin,
                                   const f32 radius, const TileMap& tileMap)
    {
        constexpr f32 TwoPi = 6.28318530717958647f;
        ShadowMapData sm{};
        sm.LightIndex = lightIndex;
        for (u32 i = 0; i < ShadowResolution; ++i)
        {
            const f32  angle = (static_cast<f32>(i) / ShadowResolution) * TwoPi;
            const Vec2 dir   = { std::cos(angle), std::sin(angle) };
            sm.Distances[i]  = RayAABBDist(origin, dir, radius, tileMap);
        }
        return sm;
    }

    void RegisterLightRenderSystem(World& world)
    {
        world.RegisterRenderSystem("LightRender", [](Scene& scene, FramePacket& packet) {
            const Camera2D& cam     = scene.GetCamera();
            const Vec2      ref     = GetRefSize(packet);
            const TileMap&  tileMap = scene.GetTileMap();

        // clang-format off
        scene.GetWorld()
                .Query<const Transform2D, const LightEmitter>()
                .Without<Disabled>()
                .ForEach(
                    [&](const Transform2D& t, const LightEmitter& le) {
                        if (!cam.IsLightVisible(t.Position, le.Radius, ref))
                            return;

                        const u32 lightIndex = static_cast<u32>(packet.Lights.size());
                        PointLight pl{};
                        pl.Position   = t.Position;
                        pl.Radius     = le.Radius;
                        pl.Intensity  = le.Intensity;
                        pl.LightColor = le.LightColor;
                        packet.Lights.push_back(pl);

                        if (le.CastsShadows && packet.ShadowMaps.size() < MaxShadowLights)
                            packet.ShadowMaps.push_back(
                                ComputeShadowMap(lightIndex, t.Position, le.Radius, tileMap));
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
    RegisterAudioSystem(world);                // spatial sound lifecycle + listener update

    // Render-collect phase — tilemap first so its sprites enter the shared sort.
    RegisterTilemapRenderSystem(world);
    RegisterSpriteRenderSystem(world);
    RegisterLightRenderSystem(world);
}
} // namespace Arcbit
