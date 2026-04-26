#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/ecs/World.h>
#include <arcbit/physics/AABB.h>
#include <arcbit/physics/Collider2D.h>
#include <arcbit/physics/SpatialHash.h>
#include <arcbit/physics/TileColliderRect.h>
#include <arcbit/render/RenderHandle.h>

#include <unordered_map>
#include <vector>

namespace Arcbit
{
struct FramePacket;
}

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

    // Per-collider state cached by the broadphase. Owner is the ECS entity that
    // registered the collider; WorldAABB is the cached bounding AABB used to
    // remove the collider from the spatial hash on Update/Unregister; the
    // remaining fields are copies of Collider2D fields needed for resolver
    // filtering without a component lookup. Active==false means the slot is
    // vacant (free-list candidate).
    struct ColliderRecord
    {
        Entity   Owner     = Entity::Invalid();
        AABB     WorldAABB = {};
        BodyKind Kind      = BodyKind::Kinematic;
        u32      Layer     = 0;
        u32      Mask      = 0;
        bool     IsTrigger = false;
        bool     Active    = false;
    };

    explicit PhysicsWorld(f32 cellSize);

    // Insert a collider associated with `entity`, located at `worldPosition`.
    // The collider data is copied; subsequent edits to the source struct do not
    // affect the broadphase until UpdateCollider is called.
    ColliderId RegisterCollider(Entity entity, const Collider2D& collider,
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

    // O(1) lookup from broadphase ColliderId to its cached record. The
    // returned reference is invalidated by Register / Unregister / Update.
    // Asserts in debug builds when `id` is out of range.
    [[nodiscard]] const ColliderRecord& GetRecord(ColliderId id) const noexcept;

    // Reverse lookup: find the ColliderId an entity registered. Returns
    // InvalidId when the entity has never registered (or has been
    // unregistered). Resolver uses this to filter "self" out of broadphase
    // results, and to call UpdateCollider after committing the new position.
    // Assumes one collider per entity — re-registration of the same entity
    // overwrites the previous mapping.
    [[nodiscard]] ColliderId FindColliderForEntity(Entity entity) const noexcept;

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
    // Pack an entity into a u64 key for the entity→ColliderId map. Generation
    // is in the upper 32 bits so reused indices with bumped generations get a
    // distinct key.
    [[nodiscard]] static constexpr u64 PackEntity(const Entity e) noexcept
    {
        return (static_cast<u64>(e.Generation) << 32) | static_cast<u64>(e.Index);
    }

    SpatialHash                         _hash;
    std::vector<ColliderRecord>         _colliders;
    std::vector<ColliderId>             _freeList;
    std::unordered_map<u64, ColliderId> _entityToCollider; // packed entity → id; one collider per entity
    TileMap*                            _tilemap   = nullptr;
    bool                                _debugDraw = false;
};
} // namespace Arcbit
