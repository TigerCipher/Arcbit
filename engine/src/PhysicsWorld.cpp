#include <arcbit/physics/PhysicsWorld.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

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

    LOG_DEBUG(Engine, "PhysicsWorld::SelfTest passed");
    #endif
}
} // namespace Arcbit
