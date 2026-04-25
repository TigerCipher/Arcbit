#include <arcbit/physics/PhysicsWorld.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/tilemap/TileMap.h>

#include <cmath>

namespace Arcbit
{
PhysicsWorld::PhysicsWorld(const f32 cellSize) : _hash(cellSize) {}

PhysicsWorld::ColliderId PhysicsWorld::RegisterCollider(const Entity      entity,
                                                        const Collider2D& collider,
                                                        const Vec2        worldPosition)
{
    const AABB worldAABB = ComputeWorldAABB(collider, worldPosition);

    ColliderId id;
    if (!_freeList.empty()) {
        id = _freeList.back();
        _freeList.pop_back();
    }
    else {
        id = static_cast<ColliderId>(_colliders.size());
        _colliders.emplace_back();
    }

    _colliders[id] = Entry{
        .Owner     = entity,
        .WorldAABB = worldAABB,
        .Kind      = collider.Kind,
        .Layer     = collider.Layer,
        .Mask      = collider.Mask,
        .IsTrigger = collider.IsTrigger,
        .Active    = true,
    };
    _hash.Insert(id, worldAABB);
    return id;
}

void PhysicsWorld::UnregisterCollider(const ColliderId id)
{
    ARCBIT_ASSERT(id < _colliders.size() && _colliders[id].Active,
                  "PhysicsWorld::UnregisterCollider called with stale or invalid id");
    if (id >= _colliders.size() || !_colliders[id].Active) return;

    _hash.Remove(id, _colliders[id].WorldAABB);
    _colliders[id].Active = false;
    _colliders[id].Owner  = Entity::Invalid();
    _freeList.push_back(id);
}

void PhysicsWorld::UpdateCollider(const ColliderId id, const Collider2D& collider,
                                  const Vec2       worldPosition)
{
    ARCBIT_ASSERT(id < _colliders.size() && _colliders[id].Active,
                  "PhysicsWorld::UpdateCollider called with stale or invalid id");
    if (id >= _colliders.size() || !_colliders[id].Active) return;

    Entry&     entry   = _colliders[id];
    const AABB newAABB = ComputeWorldAABB(collider, worldPosition);

    // Cheap early-out — if the AABB is bit-identical the cell membership is too.
    if (entry.WorldAABB.Min.X == newAABB.Min.X && entry.WorldAABB.Min.Y == newAABB.Min.Y &&
        entry.WorldAABB.Max.X == newAABB.Max.X && entry.WorldAABB.Max.Y == newAABB.Max.Y) {
        // Still refresh the layer/mask/kind cache in case the caller mutated them.
        entry.Kind      = collider.Kind;
        entry.Layer     = collider.Layer;
        entry.Mask      = collider.Mask;
        entry.IsTrigger = collider.IsTrigger;
        return;
    }

    _hash.Remove(id, entry.WorldAABB);
    entry.WorldAABB = newAABB;
    entry.Kind      = collider.Kind;
    entry.Layer     = collider.Layer;
    entry.Mask      = collider.Mask;
    entry.IsTrigger = collider.IsTrigger;
    _hash.Insert(id, newAABB);
}

void PhysicsWorld::Tick(const f32 /*dt*/)
{
    // Stub. Resolution pass lands in 22B (FreeMovement) and 22C (TileMovement).
}

void PhysicsWorld::QueryTileColliders(const AABB&                    worldAABB,
                                      std::vector<TileColliderRect>& out) const
{
    out.clear();
    if (!_tilemap) return;

    // Map the query AABB into chunk coordinates.
    const f32 tileSize  = _tilemap->GetTileSize();
    const f32 chunkSize = tileSize * static_cast<f32>(TileMap::GetChunkSize());
    const i32 cx0       = static_cast<i32>(std::floor(worldAABB.Min.X / chunkSize));
    const i32 cy0       = static_cast<i32>(std::floor(worldAABB.Min.Y / chunkSize));
    const i32 cx1       = static_cast<i32>(std::floor(worldAABB.Max.X / chunkSize));
    const i32 cy1       = static_cast<i32>(std::floor(worldAABB.Max.Y / chunkSize));

    for (i32 cy = cy0; cy <= cy1; ++cy) {
        for (i32 cx = cx0; cx <= cx1; ++cx) {
            const auto& rects = _tilemap->GetChunkColliders(cx, cy);
            // Cheap per-rect AABB filter — most chunk rects won't overlap a
            // small query (e.g. one entity's swept AABB).
            for (const auto& rect : rects)
                if (rect.WorldAABB.Overlaps(worldAABB))
                    out.push_back(rect);
        }
    }
}

usize PhysicsWorld::ColliderCount() const noexcept { return _colliders.size() - _freeList.size(); }

AABB PhysicsWorld::ComputeWorldAABB(const Collider2D& collider, const Vec2 worldPosition)
{
    const Vec2 center = {
        worldPosition.X + collider.Offset.X,
        worldPosition.Y + collider.Offset.Y
    };

    if (collider.Shape == ColliderShape::Circle) {
        return AABB::FromCenterHalfExtents(center, {collider.Radius, collider.Radius});
    }

    // Box — fast path when not rotated.
    if (collider.Rotation == 0.0f) { return AABB::FromCenterHalfExtents(center, collider.HalfExtents); }

    // Rotated box — bounding AABB of the OBB.
    // For an OBB with half-extents (hx, hy) rotated by θ:
    //   aabbHalf.x = |cos θ|*hx + |sin θ|*hy
    //   aabbHalf.y = |sin θ|*hx + |cos θ|*hy
    const f32  absCos    = std::abs(std::cos(collider.Rotation));
    const f32  absSin    = std::abs(std::sin(collider.Rotation));
    const Vec2 boundHalf = {
        absCos * collider.HalfExtents.X + absSin * collider.HalfExtents.Y,
        absSin * collider.HalfExtents.X + absCos * collider.HalfExtents.Y,
    };
    return AABB::FromCenterHalfExtents(center, boundHalf);
}

// ---------------------------------------------------------------------------
// SelfTest — register / query / move / unregister roundtrip, debug builds only.
// ---------------------------------------------------------------------------

void PhysicsWorld::SelfTest()
{
    #ifdef ARCBIT_DEBUG
    PhysicsWorld world(32.0f);

    // Two static box colliders sitting in different cells.
    const Collider2D rockShape{
        .Shape       = ColliderShape::Box,
        .HalfExtents = {6.0f, 6.0f},
        .Kind        = BodyKind::Static,
    };
    const Entity rockEntity{.Index = 1, .Generation = 0};
    const Entity treeEntity{.Index = 2, .Generation = 0};

    const ColliderId rock = world.RegisterCollider(rockEntity, rockShape, {16.0f, 16.0f});
    const ColliderId tree = world.RegisterCollider(treeEntity, rockShape, {200.0f, 16.0f});

    ARCBIT_ASSERT(world.ColliderCount() == 2,
                  "PhysicsWorld::SelfTest: expected two registered colliders");

    std::vector<ColliderId> result;

    // Query a region around the rock — should find rock, not tree.
    world.QueryAABB(AABB::FromCenterHalfExtents({16.0f, 16.0f}, {8.0f, 8.0f}), result);
    ARCBIT_ASSERT(result.size() == 1 && result[0] == rock,
                  "PhysicsWorld::SelfTest: rock query returned wrong set");

    // Move the rock far away. The old query region should now be empty; a query
    // at the new position should find it.
    world.UpdateCollider(rock, rockShape, {1000.0f, 1000.0f});
    world.QueryAABB(AABB::FromCenterHalfExtents({16.0f, 16.0f}, {8.0f, 8.0f}), result);
    ARCBIT_ASSERT(result.empty(),
                  "PhysicsWorld::SelfTest: stale AABB cache — rock still in old cell");
    world.QueryAABB(AABB::FromCenterHalfExtents({1000.0f, 1000.0f}, {8.0f, 8.0f}), result);
    ARCBIT_ASSERT(result.size() == 1 && result[0] == rock,
                  "PhysicsWorld::SelfTest: rock missing from new cell after Update");

    // Unregister the rock — its slot should free, count drops to 1.
    world.UnregisterCollider(rock);
    ARCBIT_ASSERT(world.ColliderCount() == 1,
                  "PhysicsWorld::SelfTest: count did not drop after Unregister");
    world.QueryAABB(AABB::FromCenterHalfExtents({1000.0f, 1000.0f}, {8.0f, 8.0f}), result);
    ARCBIT_ASSERT(result.empty(),
                  "PhysicsWorld::SelfTest: rock still in hash after Unregister");

    // Next register should reuse the freed slot.
    const Entity     boxEntity{.Index = 3, .Generation = 0};
    const ColliderId reused = world.RegisterCollider(boxEntity, rockShape, {50.0f, 50.0f});
    ARCBIT_ASSERT(reused == rock,
                  "PhysicsWorld::SelfTest: free-list did not reuse the vacant slot");

    // Circle and rotated-box AABB sanity — sizes match expectation.
    const Collider2D circleShape{
        .Shape  = ColliderShape::Circle,
        .Radius = 10.0f,
    };
    const AABB circleAABB = ComputeWorldAABB(circleShape, {0.0f, 0.0f});
    ARCBIT_ASSERT(circleAABB.Min.X == -10.0f && circleAABB.Max.X == 10.0f,
                  "PhysicsWorld::SelfTest: circle AABB extents wrong");

    const Collider2D rotatedShape{
        .Shape       = ColliderShape::Box,
        .Rotation    = 1.5707963267948966f, // 90°
        .HalfExtents = {4.0f, 16.0f},
    };
    const AABB rotatedAABB = ComputeWorldAABB(rotatedShape, {0.0f, 0.0f});
    // 90° rotation swaps the half-extents.
    constexpr f32 tol = 0.001f;
    ARCBIT_ASSERT(std::abs(rotatedAABB.HalfExtents().X - 16.0f) < tol &&
                  std::abs(rotatedAABB.HalfExtents().Y - 4.0f) < tol,
                  "PhysicsWorld::SelfTest: rotated-box bounding AABB wrong");

    // ---- Tile collider greedy mesh -----------------------------------------
    // Standalone TileMap configured with a single solid TileDef. We don't load
    // any atlas — IsSolid only needs FindTileDef + GetTile to work, so writing
    // tile IDs directly with SetTile and registering a TileDef is enough.
    {
        TileMap tilemap;
        tilemap.SetTileSize(32.0f);

        // Tile id 1 is solid. Default Layer = Wall, BlockedFrom empty.
        TileDef wallDef{};
        wallDef.Solid = true;
        tilemap.RegisterTile(1, wallDef);

        PhysicsWorld pw(32.0f);
        pw.SetTileMap(&tilemap);

        std::vector<TileColliderRect> rects;

        // Empty tilemap → no rects.
        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.empty(),
                      "PhysicsWorld::SelfTest: empty tilemap should produce no rects");

        // Pattern A: 3-tile horizontal line at (0,0)-(2,0). Greedy must merge
        // into one 3x1 rect.
        constexpr u32 OBJECTS = 1;
        tilemap.SetTile(0, 0, OBJECTS, 1);
        tilemap.SetTile(1, 0, OBJECTS, 1);
        tilemap.SetTile(2, 0, OBJECTS, 1);

        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 1,
                      "PhysicsWorld::SelfTest: 3-tile row should greedy-merge to 1 rect");
        ARCBIT_ASSERT(std::abs(rects[0].WorldAABB.Size().X - 96.0f) < tol &&
                      std::abs(rects[0].WorldAABB.Size().Y - 32.0f) < tol,
                      "PhysicsWorld::SelfTest: 3x1 rect dimensions wrong");

        // Pattern B: Add a 2x2 block at (5,0)-(6,1). Now the chunk holds
        // a 3x1 rect plus a 2x2 rect — non-adjacent → no merging.
        tilemap.SetTile(5, 0, OBJECTS, 1);
        tilemap.SetTile(6, 0, OBJECTS, 1);
        tilemap.SetTile(5, 1, OBJECTS, 1);
        tilemap.SetTile(6, 1, OBJECTS, 1);

        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 2,
                      "PhysicsWorld::SelfTest: non-adjacent shapes should produce 2 rects");

        // Coverage check: total area must equal 3 + 4 = 7 tiles worth.
        constexpr f32 tileArea  = 32.0f * 32.0f;
        f32           totalArea = 0.0f;
        for (const auto& r : rects)
            totalArea += r.WorldAABB.Size().X * r.WorldAABB.Size().Y;
        ARCBIT_ASSERT(std::abs(totalArea - 7.0f * tileArea) < tol,
                      "PhysicsWorld::SelfTest: greedy mesh coverage incorrect");

        // Pattern C: SetTile invalidates the chunk cache. Erasing the 3-tile
        // row should leave just the 2x2 block.
        tilemap.SetTile(0, 0, OBJECTS, 0);
        tilemap.SetTile(1, 0, OBJECTS, 0);
        tilemap.SetTile(2, 0, OBJECTS, 0);
        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 1 &&
                      std::abs(rects[0].WorldAABB.Size().X - 64.0f) < tol &&
                      std::abs(rects[0].WorldAABB.Size().Y - 64.0f) < tol,
                      "PhysicsWorld::SelfTest: cache not invalidated by SetTile");

        // Pattern D: Two solid TileDefs with *different* Layer values must NOT
        // merge even when adjacent — they belong to different collision classes.
        TileDef wallDef2{};
        wallDef2.Solid = true;
        wallDef2.Layer = CollisionLayers::Prop; // distinct from wallDef's Wall
        tilemap.RegisterTile(2, wallDef2);

        // Clear the 2x2 block first to start with a clean slate at (10, 0).
        tilemap.SetTile(5, 0, OBJECTS, 0);
        tilemap.SetTile(6, 0, OBJECTS, 0);
        tilemap.SetTile(5, 1, OBJECTS, 0);
        tilemap.SetTile(6, 1, OBJECTS, 0);

        tilemap.SetTile(10, 0, OBJECTS, 1); // Wall
        tilemap.SetTile(11, 0, OBJECTS, 2); // Prop

        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 2,
                      "PhysicsWorld::SelfTest: tiles with differing Layer must not merge");

        // Pattern E: chunks limit the mesh — solid tiles spanning a chunk
        // boundary form one rect per chunk. ChunkSize == 16 with tileSize 32
        // means tile X coordinates [0..15] are chunk 0; [16..31] are chunk 1.
        // Clear and lay a 2-tile horizontal strip across the boundary at (15,5)-(16,5).
        tilemap.SetTile(10, 0, OBJECTS, 0);
        tilemap.SetTile(11, 0, OBJECTS, 0);
        tilemap.SetTile(15, 5, OBJECTS, 1);
        tilemap.SetTile(16, 5, OBJECTS, 1);
        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 2,
                      "PhysicsWorld::SelfTest: chunk boundary should split greedy mesh");

        LOG_DEBUG(Engine, "PhysicsWorld::SelfTest: greedy tile mesh passed");
    }

    LOG_DEBUG(Engine, "PhysicsWorld::SelfTest passed");
    #endif
}
} // namespace Arcbit
