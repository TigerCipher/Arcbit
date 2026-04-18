#pragma once

#include <arcbit/ecs/World.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/scene/WorldConfig.h>
#include <arcbit/tilemap/TileMap.h>

namespace Arcbit {

struct FramePacket;

// ---------------------------------------------------------------------------
// Scene — top-level container for a running game world.
//
// Owns the ECS World, the active Camera2D, and the WorldConfig resource.
// Application drives it via Update() (fixed tick) and CollectRenderData()
// (variable rate). Game code accesses the scene through Application::GetScene().
// ---------------------------------------------------------------------------
class Scene
{
public:
    Scene();

    // ECS world — create/destroy entities, add/remove components, register systems.
    [[nodiscard]] World&       GetWorld()   { return _world; }
    [[nodiscard]] Camera2D&    GetCamera()  { return _camera; }
    [[nodiscard]] WorldConfig& GetConfig()  { return _config; }
    [[nodiscard]] TileMap&     GetTileMap() { return _tileMap; }

    // Called by Application each fixed tick. Advances the camera and runs all
    // registered update systems in registration order.
    void Update(f32 dt);

    // Called by Application each frame after OnRender. Sets camera packet fields
    // and runs all registered render systems (filling packet.Sprites and .Lights).
    void CollectRenderData(FramePacket& packet);

private:
    World       _world;
    Camera2D    _camera;
    WorldConfig _config;
    TileMap     _tileMap;
};

} // namespace Arcbit
