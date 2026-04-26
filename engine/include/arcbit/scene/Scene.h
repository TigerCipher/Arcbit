#pragma once

#include <arcbit/ecs/World.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/scene/WorldConfig.h>
#include <arcbit/tilemap/TileMap.h>

#include <memory>

namespace Arcbit {

struct FramePacket;
class PhysicsWorld;

// ---------------------------------------------------------------------------
// Scene — top-level container for a running game world.
//
// Owns the ECS World, the active Camera2D, the WorldConfig resource, the
// TileMap, and (lazily) the PhysicsWorld. Application drives it via Update()
// (fixed tick) and CollectRenderData() (variable rate). Game code accesses
// the scene through Application::GetScene().
// ---------------------------------------------------------------------------
class Scene
{
public:
    Scene();
    ~Scene(); // out-of-line so PhysicsWorld only needs to be complete in the .cpp

    // ECS world — create/destroy entities, add/remove components, register systems.
    [[nodiscard]] World&       GetWorld()   { return _world; }
    [[nodiscard]] Camera2D&    GetCamera()  { return _camera; }
    [[nodiscard]] WorldConfig& GetConfig()  { return _config; }
    [[nodiscard]] TileMap&     GetTileMap() { return _tileMap; }

    // PhysicsWorld is lazy-constructed on first call using WorldConfig.TileSize
    // as the spatial-hash cell size, and is automatically wired to the scene's
    // TileMap. **Constraint**: set GetConfig().TileSize *before* the first
    // call (typically the first thing OnStart does), otherwise the hash is
    // sized for the default (32 px) and changing TileSize afterwards has no
    // effect on the broadphase grid.
    [[nodiscard]] PhysicsWorld& GetPhysics();

    // Called by Application each fixed tick. Advances the camera and runs all
    // registered update systems in registration order.
    void Update(f32 dt);

    // Called by Application each frame after OnRender. Sets camera packet fields
    // and runs all registered render systems (filling packet.Sprites and .Lights).
    void CollectRenderData(FramePacket& packet);

private:
    World                         _world;
    Camera2D                      _camera;
    WorldConfig                   _config;
    TileMap                       _tileMap;
    std::unique_ptr<PhysicsWorld> _physics; // lazy; created on first GetPhysics()
};

} // namespace Arcbit
