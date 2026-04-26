#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/ecs/World.h>
#include <arcbit/physics/AABB.h>
#include <arcbit/physics/Collider2D.h>
#include <arcbit/physics/SpatialHash.h>
#include <arcbit/physics/TileColliderRect.h>
#include <arcbit/render/RenderHandle.h>

#include <vector>

namespace Arcbit { struct FramePacket; }

namespace Arcbit
{
class TileMap; // forward decl — narrowphase against tile colliders lands later

// PhysicsWorld owns the broadphase (SpatialHash) plus the cache of per-collider
// world AABBs that callers would otherwise have to track themselves to satisfy
// SpatialHash::Remove. Each registered collider gets a stable ColliderId until
// it is unregistered.
//
// Per-frame movement systems should:
//   1. Register colliders at entity creation (or on first frame).
//   2. Call UpdateCollider whenever the entity's world position changes.
//   3. Unregister at entity destruction.
//
// Tick(dt) is the entry point for the (future) collision resolution pass — a
// stub for now so the call site can be wired ahead of the resolver landing.
class PhysicsWorld
{
public:
    using ColliderId                      = SpatialHash::ColliderId;
    static constexpr ColliderId InvalidId = ~0u;

    explicit PhysicsWorld(f32 cellSize);

    // Insert a collider associated with `entity`, located at `worldPosition`.
    // The collider data is copied; subsequent edits to the source struct do not
    // affect the broadphase until UpdateCollider is called.
    [[nodiscard]] ColliderId RegisterCollider(Entity entity, const Collider2D& collider,
                                              Vec2   worldPosition);

    // Drop a previously-registered collider. The id becomes invalid; callers
    // should not reuse it. Subsequent calls on a stale id are a no-op (debug
    // builds assert).
    void UnregisterCollider(ColliderId id);

    // Recompute the world AABB for `id` based on the supplied collider data
    // and world position, swap it into the broadphase, and update the cache.
    // Cheaper than Unregister + Register because the slot stays put.
    void UpdateCollider(ColliderId id, const Collider2D& collider, Vec2 worldPosition);

    // Per-tick stub. Will own the resolution pass once the narrowphase exists.
    void Tick(f32 dt);

    // Pure broadphase query. Caller must run a narrowphase test on each result.
    void QueryAABB(const AABB& worldAABB, std::vector<ColliderId>& out) const { _hash.Query(worldAABB, out); }

    // Append all greedy-meshed tile collider rects whose chunks overlap the
    // query AABB. Cleared first. No-op (just clears `out`) if no TileMap is
    // attached. Caller still narrowphases each rect — chunk overlap doesn't
    // mean the individual rect overlaps the query.
    void QueryTileColliders(const AABB& worldAABB, std::vector<TileColliderRect>& out) const;

    // Tilemap pointer for the (future) tile-synthesized collider path. Non-owning;
    // the Scene owns the tilemap and outlives the PhysicsWorld.
    void                   SetTileMap(TileMap* tilemap) noexcept { _tilemap = tilemap; }
    [[nodiscard]] TileMap* GetTileMap() const noexcept { return _tilemap; }

    [[nodiscard]] const SpatialHash& Hash() const noexcept { return _hash; }
    [[nodiscard]] usize              ColliderCount() const noexcept;

    // ---- Debug draw (developer/editor tool, not a player setting) ---------
    // Toggled at runtime by a dev key binding in demo builds and by the editor
    // IPC channel in Phase 40. CollectDebugDraw is a no-op while disabled.
    void               SetDebugDraw(bool enabled) noexcept { _debugDraw = enabled; }
    [[nodiscard]] bool GetDebugDraw() const noexcept { return _debugDraw; }

    // Emit outline rectangles for every registered collider (color-coded by
    // BodyKind / IsTrigger) and for every cached tile collider rect. Caller
    // supplies a 1×1 white texture + sampler used for the outline strokes —
    // the engine's UI white texture (RenderThread::GetUIWhiteTexture) is the
    // intended source. No-op if debug draw is disabled.
    void CollectDebugDraw(FramePacket&  packet,
                          TextureHandle whiteTex,
                          SamplerHandle whiteSampler) const;

    // Compute a world-space bounding AABB for a collider at the given position.
    // Handles axis-aligned box (fast path), rotated box (OBB bounds), and circle.
    [[nodiscard]] static AABB ComputeWorldAABB(const Collider2D& collider, Vec2 worldPosition);

    // Debug-only roundtrip self test (register / query / move / unregister).
    static void SelfTest();

private:
    struct Entry
    {
        Entity   Owner     = Entity::Invalid();
        AABB     WorldAABB = {}; // cached so Remove can reach the right cells
        BodyKind Kind      = BodyKind::Kinematic;
        u32      Layer     = 0; // Layer/Mask cached for narrowphase filtering
        u32      Mask      = 0; // (consumed in a later slice)
        bool     IsTrigger = false;
        bool     Active    = false; // false = slot vacant (free-list candidate)
    };

    SpatialHash             _hash;
    std::vector<Entry>      _colliders;
    std::vector<ColliderId> _freeList;
    TileMap*                _tilemap   = nullptr;
    bool                    _debugDraw = false;
};
} // namespace Arcbit
