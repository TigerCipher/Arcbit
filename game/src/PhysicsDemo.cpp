// ---------------------------------------------------------------------------
// Phase 22A debug-draw verification demo.
//
// Sets up a tiny scene to make the physics debug overlay easy to eyeball:
//   - 17×17 grass field
//   - 4×4 water square in the middle, marked Solid → tile collider mesh
//   - Player as a circle collider, WASD to move (no resolver yet, so the
//     player walks *through* water — point of this demo is to *see* the
//     colliders, not block them)
//
// Press F2 to toggle physics debug draw (defaults to ON for instant feedback).
// ---------------------------------------------------------------------------

#include "Common.h"

#include <arcbit/app/Application.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/World.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/physics/Collider2D.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/WorldConfig.h>
#include <arcbit/tilemap/TileMap.h>

#include <memory>

using namespace Arcbit;

namespace
{
constexpr ActionID ActionMoveLeft     = MakeAction("Move_Left");
constexpr ActionID ActionMoveRight    = MakeAction("Move_Right");
constexpr ActionID ActionMoveUp       = MakeAction("Move_Up");
constexpr ActionID ActionMoveDown     = MakeAction("Move_Down");
constexpr ActionID ActionDebugPhysics = MakeAction("Debug_Physics");
constexpr ActionID ActionToggleGrid = MakeAction("Debug_ToggleGrid");

constexpr f32 TileSize     = 64.0f;
constexpr u32 GrassTileId  = 1;   // base of grass.tileatlas.json
constexpr u32 WaterTileId  = 409; // base of water.tileatlas.json
constexpr i32 GrassExtent  = 8;   // grass field is [-8..+8] tiles each axis
constexpr i32 WaterMin     = -2;  // water is [-2..+1] each axis (4×4)
constexpr i32 WaterMax     = 1;
} // namespace

class PhysicsDemo : public Application
{
public:
    PhysicsDemo()
        : Application({ .Title = "Arcbit — Physics Demo (F2 toggles colliders)",
                        .Width = 1920, .Height = 1080 })
    {}

protected:
    void OnStart() override
    {
        _sampler  = Demo::CreateNearestRepeatSampler(GetDevice());
        _whiteTex = Demo::CreateWhiteTexture(GetDevice());
        
        TextureDesc gd{};
        gd.Width              = 1;
        gd.Height             = 1;
        gd.Format             = Format::RGBA8_UNorm;
        gd.Usage              = TextureUsage::Sampled | TextureUsage::Transfer;
        gd.DebugName          = "Debug_SolidWhite";
        _gridTex              = GetDevice().CreateTexture(gd);
        constexpr u8 white[4] = {255, 255, 255, 255};
        GetDevice().UploadTexture(_gridTex, white, sizeof(white));

        SamplerDesc sd{};
        sd.MinFilter = Filter::Nearest;
        sd.MagFilter = Filter::Nearest;
        sd.AddressU  = AddressMode::ClampToEdge;
        sd.AddressV  = AddressMode::ClampToEdge;
        sd.DebugName = "Debug_GridSampler";
        _gridSampler = GetDevice().CreateSampler(sd);

        GetScene().GetConfig().TileSize = TileSize;
        GetScene().GetCamera().Zoom     = 1.5f;

        TileMap& tm = GetScene().GetTileMap();
        tm.SetTileSize(TileSize);
        ARCBIT_VERIFY(Demo::LoadGrassAndWaterAtlases(tm, GetTextures(), _sampler),
                      "PhysicsDemo: failed to load tile atlases");

        // Water tile id 409 is solid for this demo. Overrides whatever
        // water.tileatlas.json says (which is fine — TileDef::Solid is the
        // only field this demo cares about).
        TileDef waterDef{};
        waterDef.Solid = true;
        tm.RegisterTile(WaterTileId, waterDef);

        // Generate the scene: grass everywhere, water in the middle.
        for (i32 ty = -GrassExtent; ty <= GrassExtent; ++ty)
            for (i32 tx = -GrassExtent; tx <= GrassExtent; ++tx)
                tm.SetTile(tx, ty, 0, GrassTileId);

        for (i32 ty = WaterMin; ty <= WaterMax; ++ty)
            for (i32 tx = WaterMin; tx <= WaterMax; ++tx)
                tm.SetTile(tx, ty, 0, WaterTileId);

        // Input — WASD plus the F2 debug toggle. No pause, no UI.
        GetInput().RegisterAction(ActionMoveLeft,     "Move_Left",     "Move Left",  "Movement");
        GetInput().RegisterAction(ActionMoveRight,    "Move_Right",    "Move Right", "Movement");
        GetInput().RegisterAction(ActionMoveUp,       "Move_Up",       "Move Up",    "Movement");
        GetInput().RegisterAction(ActionMoveDown,     "Move_Down",     "Move Down",  "Movement");
        GetInput().RegisterAction(ActionDebugPhysics, "Debug_Physics", "Toggle Physics Debug", "Debug");
        GetInput().RegisterAction(ActionToggleGrid,   "Debug_ToggleGrid", "Toggle Grid", "Debug");

        GetInput().BindKey(ActionMoveLeft,     Key::A);
        GetInput().BindKey(ActionMoveRight,    Key::D);
        GetInput().BindKey(ActionMoveUp,       Key::W);
        GetInput().BindKey(ActionMoveDown,     Key::S);
        GetInput().BindKey(ActionDebugPhysics, Key::F2);
        GetInput().BindKey(ActionToggleGrid,   Key::G);

        // Player entity — small blue square sprite + circle collider. The
        // sprite is just so the eye has something concrete to attach the
        // collider outline to; the demo doesn't depend on it.
        auto& world                                = GetScene().GetWorld();
        _player                                    = world.CreateEntity();
        world.GetComponent<Tag>(_player)->Value    = "Player";
        auto* t                                    = world.GetTransform(_player);
        t->Position                                = { TileSize * 5.0f, 0.0f };
        t->Scale                                   = { TileSize * 0.7f, TileSize * 0.7f };
        world.AddComponent<SpriteRenderer>(_player, SpriteRenderer{
                                              .Texture = _whiteTex,
                                              .Sampler = _sampler,
                                              .Tint    = { 0.3f, 0.5f, 1.0f, 1.0f },
                                              .Layer   = 0,
                                          });
        world.AddComponent<CameraTarget>(_player, CameraTarget{ .Lag = 0.05f });

        // PhysicsWorld — owns the broadphase + tile collider mesher.
        // Cell size matches tile size so chunked tile colliders bucket cleanly.
        _physics = std::make_unique<PhysicsWorld>(TileSize);
        _physics->SetTileMap(&tm);
        _physics->SetDebugDraw(true); // ON by default so the demo is self-explanatory

        // Player collider — circle at half the player's visual size.
        _playerCollider = Collider2D{
            .Shape  = ColliderShape::Circle,
            .Radius = TileSize * 0.35f,
            .Kind   = BodyKind::Kinematic,
            .Layer  = CollisionLayers::Player,
        };
        _playerColliderId = _physics->RegisterCollider(_player, _playerCollider, t->Position);

        LOG_INFO(Game, "PhysicsDemo ready — WASD to move, F2 toggles collider overlay");
    }

    void OnUpdate(const f32 dt) override
    {
        Vec2 dir{};
        if (GetInput().IsPressed(ActionMoveLeft))  dir.X -= 1.0f;
        if (GetInput().IsPressed(ActionMoveRight)) dir.X += 1.0f;
        if (GetInput().IsPressed(ActionMoveUp))    dir.Y -= 1.0f;
        if (GetInput().IsPressed(ActionMoveDown))  dir.Y += 1.0f;

        // Simple unbounded movement — collision response lands in 22B.
        constexpr f32 PlayerSpeed = 240.0f; // world px/sec
        auto*         t           = GetScene().GetWorld().GetTransform(_player);
        t->Position += dir * (PlayerSpeed * dt);

        // Keep the broadphase / AABB cache in sync with the player's position.
        _physics->UpdateCollider(_playerColliderId, _playerCollider, t->Position);

        if (GetInput().JustPressed(ActionDebugPhysics)) {
            _physics->SetDebugDraw(!_physics->GetDebugDraw());
            LOG_INFO(Game, "Physics debug draw: {}", _physics->GetDebugDraw() ? "ON" : "OFF");
        }
        if (GetInput().JustPressed(ActionToggleGrid)) ToggleDebugGrid();
    }

    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        // Emit collider outlines on top of the scene. Sprites already collected
        // by the scene's Y-sort + tilemap render pipeline; we just append.
        _physics->CollectDebugDraw(packet, _whiteTex, _sampler);
        
        if (_showGrid) SubmitDebugGrid(packet, {1920, 1080});
    }

    void OnShutdown() override
    {
        if (_physics && _playerColliderId != PhysicsWorld::InvalidId)
            _physics->UnregisterCollider(_playerColliderId);
        _physics.reset();

        if (_whiteTex.IsValid()) GetDevice().DestroyTexture(_whiteTex);
        if (_sampler.IsValid())  GetDevice().DestroySampler(_sampler);
        
        if (_gridTex.IsValid())
            GetDevice().DestroyTexture(_gridTex);
        if (_gridSampler.IsValid())
            GetDevice().DestroySampler(_gridSampler);
    }

private:
    SamplerHandle                 _sampler;
    TextureHandle                 _whiteTex;
    Entity                        _player           = Entity::Invalid();
    std::unique_ptr<PhysicsWorld> _physics;
    Collider2D                    _playerCollider;
    PhysicsWorld::ColliderId      _playerColliderId = PhysicsWorld::InvalidId;
    
    TextureHandle _gridTex;
    SamplerHandle _gridSampler;
    
        // -----------------------------------------------------------------------
    // Debug grid
    // -----------------------------------------------------------------------

    void SubmitDebugGrid(FramePacket& packet, const Vec2 viewport)
    {
        const Camera2D& cam    = GetScene().GetCamera();
        const Vec2      camPos = cam.GetEffectivePosition();
        const f32       halfW  = (viewport.X / cam.Zoom) * 0.5f;
        const f32       halfH  = (viewport.Y / cam.Zoom) * 0.5f;

        const auto fc = static_cast<i32>(std::floor((camPos.X - halfW) / TileSize)) - 1;
        const auto lc = static_cast<i32>(std::ceil((camPos.X + halfW) / TileSize)) + 1;
        const auto fr = static_cast<i32>(std::floor((camPos.Y - halfH) / TileSize)) - 1;
        const auto lr = static_cast<i32>(std::ceil((camPos.Y + halfH) / TileSize)) + 1;

        const f32       totalH = static_cast<f32>(lr - fr) * TileSize;
        const f32       totalW = static_cast<f32>(lc - fc) * TileSize;
        const f32       origX  = static_cast<f32>(fc) * TileSize;
        const f32       origY  = static_cast<f32>(fr) * TileSize;
        const f32       thick  = std::max(1.0f, 1.0f / cam.Zoom);
        constexpr Color GCol   = {1.0f, 1.0f, 1.0f, 0.25f};
        constexpr i32   GLay   = 999999;

        for (i32 col = fc; col <= lc; ++col) {
            Sprite s{};
            s.Texture  = _gridTex;
            s.Sampler  = _gridSampler;
            s.Position = {col * TileSize + (TileSize * 0.5f), origY + totalH * 0.5f};
            s.Size     = {thick, totalH};
            s.Tint     = GCol;
            s.Layer    = GLay;
            packet.Sprites.push_back(s);
        }
        for (i32 row = fr; row <= lr; ++row) {
            Sprite s{};
            s.Texture  = _gridTex;
            s.Sampler  = _gridSampler;
            s.Position = {origX + totalW * 0.5f, row * TileSize + (TileSize * 0.5f)};
            s.Size     = {totalW, thick};
            s.Tint     = GCol;
            s.Layer    = GLay;
            packet.Sprites.push_back(s);
        }
    }
};

// Entry point — main.cpp calls this when ARCBIT_DEMO_PHYSICS is selected (default).
void RunPhysicsDemo()
{
    PhysicsDemo demo;
    demo.Run();
}
