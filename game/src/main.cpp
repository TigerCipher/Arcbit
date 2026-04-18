#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/ecs/World.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/scene/Scene.h>

#include <cmath>

using namespace Arcbit;

// ---------------------------------------------------------------------------
// ArcbitGame — demo Application subclass
//
// Phase 14: sprite batcher demo — world-space sprites, multi-texture, multi-layer.
// Phase 15: Camera2D demo — WASD pan, scroll-wheel zoom, mouse-following point light.
// Phase 16: ECS migration — sprites and lights are ECS entities; SpriteRenderSystem
//           and LightRenderSystem fill FramePacket. Camera2D lives in the Scene.
// ---------------------------------------------------------------------------
class ArcbitGame : public Application
{
public:
    ArcbitGame() : Application({ .Title = "Arcbit", .Width = 1920, .Height = 1080 }) {}

protected:
    // -----------------------------------------------------------------------
    // OnStart — load assets, create samplers, register input, create entities.
    // -----------------------------------------------------------------------
    void OnStart() override
    {
        GetScene().GetConfig().TileSize = TileSize;

        // --- Input ---
        GetInput().RegisterAction(ActionToggleGrid, "Debug_ToggleGrid");
        GetInput().BindKey(ActionToggleGrid, Key::G);

        GetInput().RegisterAction(ActionMoveLeft, "Move_Left");
        GetInput().RegisterAction(ActionMoveRight, "Move_Right");
        GetInput().RegisterAction(ActionMoveUp, "Move_Up");
        GetInput().RegisterAction(ActionMoveDown, "Move_Down");
        GetInput().RegisterAction(ActionInteract, "Interact");
        GetInput().RegisterAction(ActionSprint, "Sprint");

        GetInput().BindKey(ActionMoveLeft, Key::A);
        GetInput().BindKey(ActionMoveLeft, Key::Left);
        GetInput().BindKey(ActionMoveRight, Key::D);
        GetInput().BindKey(ActionMoveRight, Key::Right);
        GetInput().BindKey(ActionMoveUp, Key::W);
        GetInput().BindKey(ActionMoveUp, Key::Up);
        GetInput().BindKey(ActionMoveDown, Key::S);
        GetInput().BindKey(ActionMoveDown, Key::Down);
        GetInput().BindKey(ActionInteract, Key::E);
        GetInput().BindKey(ActionInteract, Key::Enter);
        GetInput().BindKey(ActionSprint, Key::LeftShift);

        GetInput().BindGamepadButton(ActionInteract, GamepadButton::South);
        GetInput().BindGamepadButton(ActionSprint, GamepadButton::RightShoulder);
        GetInput().BindGamepadAxis(ActionMoveLeft, GamepadAxis::LeftX);
        GetInput().BindGamepadAxis(ActionMoveRight, GamepadAxis::LeftX);
        GetInput().BindGamepadAxis(ActionMoveUp, GamepadAxis::LeftY);
        GetInput().BindGamepadAxis(ActionMoveDown, GamepadAxis::LeftY);

        // --- Plain textures ---
        _floorTex = GetTextures().Load("assets/textures/floor.jpg");
        ARCBIT_ASSERT(_floorTex.IsValid(), "Failed to load floor texture");

        // --- Sprite assets ---
        _skeletonSheet = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Enemies/Skeleton.json", GetTextures());
        _slimeSheet    = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Enemies/Slime_Green.json", GetTextures());
        _chickenSheet  = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Animals/Chicken/Chicken.json", GetTextures());
        _pigSheet      = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Animals/Pig/Pig.json", GetTextures());
        _treeSheet     = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Outdoor decoration/Oak_Tree.json", GetTextures());

        ARCBIT_ASSERT(_skeletonSheet.IsValid(), "Failed to load skeleton sprite");
        ARCBIT_ASSERT(_slimeSheet.IsValid(), "Failed to load slime sprite");
        ARCBIT_ASSERT(_chickenSheet.IsValid(), "Failed to load chicken sprite");
        ARCBIT_ASSERT(_pigSheet.IsValid(), "Failed to load pig sprite");
        ARCBIT_ASSERT(_treeSheet.IsValid(), "Failed to load tree sprite");

        _playerSheet = SpriteSheet::Load("assets/spritesheets/player.json", GetTextures());
        if (_playerSheet.IsValid())
        {
            LOG_INFO(Game, "Player sheet loaded - {}x{} grid, {} tiles", _playerSheet.TileColumns(), _playerSheet.TileRows(),
                     _playerSheet.TileCount());
        }

        // --- Samplers ---
        SamplerDesc nearestDesc{};
        nearestDesc.MinFilter = Filter::Nearest;
        nearestDesc.MagFilter = Filter::Nearest;
        nearestDesc.AddressU  = AddressMode::Repeat;
        nearestDesc.AddressV  = AddressMode::Repeat;
        nearestDesc.DebugName = "NearestRepeat";
        _sampler              = GetDevice().CreateSampler(nearestDesc);
        ARCBIT_ASSERT(_sampler.IsValid(), "Failed to create sampler");

        SamplerDesc linearDesc{};
        linearDesc.MinFilter = Filter::Linear;
        linearDesc.MagFilter = Filter::Linear;
        linearDesc.AddressU  = AddressMode::Repeat;
        linearDesc.AddressV  = AddressMode::Repeat;
        linearDesc.DebugName = "LinearRepeat";
        _floorSampler        = GetDevice().CreateSampler(linearDesc);
        ARCBIT_ASSERT(_floorSampler.IsValid(), "Failed to create floor sampler");

        // --- Debug grid texture — 1×1 white pixel ---
        {
            TextureDesc desc{};
            desc.Width     = 1;
            desc.Height    = 1;
            desc.Format    = Format::RGBA8_UNorm;
            desc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
            desc.DebugName = "Debug_SolidWhite";
            _gridTex       = GetDevice().CreateTexture(desc);

            constexpr u8 white[4] = { 255, 255, 255, 255 };
            GetDevice().UploadTexture(_gridTex, white, sizeof(white));

            SamplerDesc sd{};
            sd.MinFilter = Filter::Nearest;
            sd.MagFilter = Filter::Nearest;
            sd.AddressU  = AddressMode::ClampToEdge;
            sd.AddressV  = AddressMode::ClampToEdge;
            sd.DebugName = "Debug_GridSampler";
            _gridSampler = GetDevice().CreateSampler(sd);
        }

        CreateEntities();
    }

    // -----------------------------------------------------------------------
    // OnUpdate — camera WASD pan, zoom, shake demo.
    // -----------------------------------------------------------------------
    void OnUpdate(const f32 dt) override
    {
        // Scene::Update (called after this) handles camera.Update(dt).
        Camera2D& cam = GetScene().GetCamera();

        constexpr float CameraPanSpeed = 400.0f;
        const float     sprint         = GetInput().IsPressed(ActionSprint) ? 3.0f : 1.0f;
        const float     speed          = CameraPanSpeed * sprint;

        if (GetInput().IsPressed(ActionMoveLeft))
            cam.Position.X -= speed * dt;
        if (GetInput().IsPressed(ActionMoveRight))
            cam.Position.X += speed * dt;
        if (GetInput().IsPressed(ActionMoveUp))
            cam.Position.Y -= speed * dt;
        if (GetInput().IsPressed(ActionMoveDown))
            cam.Position.Y += speed * dt;

        if (GetInput().JustPressed(ActionToggleGrid))
            _showGrid = !_showGrid;
        if (GetInput().JustPressed(ActionInteract))
            cam.AddTrauma(0.6f);

        // Update mouse-following light entity position.
        UpdateMouseLight();
    }

    // -----------------------------------------------------------------------
    // OnRender — set packet constants and push any manual (non-ECS) sprites.
    // Scene::CollectRenderData (called after this by Application) fills the
    // camera fields and runs SpriteRenderSystem / LightRenderSystem.
    // -----------------------------------------------------------------------
    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor  = Color{ 0.1f, 0.1f, 0.15f, 1.0f };
        packet.ReferenceSize = { ViewportW, ViewportH };

        if (_showGrid)
            SubmitDebugGrid(packet, { ViewportW, ViewportH });
    }

    // -----------------------------------------------------------------------
    // OnShutdown — destroy GPU resources.
    // -----------------------------------------------------------------------
    void OnShutdown() override
    {
        GetDevice().DestroyTexture(_gridTex);
        GetDevice().DestroySampler(_gridSampler);
        GetDevice().DestroySampler(_floorSampler);
        GetDevice().DestroySampler(_sampler);
    }

private:
    // -----------------------------------------------------------------------
    // CreateEntities — populate the ECS world with sprites and lights.
    // -----------------------------------------------------------------------

    // Helper: create one sprite entity with the given attributes.
    Entity MakeSpriteEntity(const Vec2 pos, const Vec2 size, const TextureHandle tex, const SamplerHandle samp, const i32 layer,
                            const UVRect uv = {})
    {
        auto&        world = GetScene().GetWorld();
        const Entity e     = world.CreateEntity();
        auto*        t     = world.GetComponent<Transform2D>(e);
        t->Position        = pos;
        t->Scale           = size;
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                                  .Texture = tex,
                                                  .Sampler = samp,
                                                  .UV      = uv,
                                                  .Layer   = layer,
                                              });
        
        return e;
    }

    Entity MakeDyingSpriteEntity(const Vec2 pos, const Vec2 size, const TextureHandle tex, const SamplerHandle samp,
                                 const i32 layer, const UVRect uv = {})
    {
        auto&        world = GetScene().GetWorld();
        const Entity e     = world.CreateEntity();
        auto*        t     = world.GetComponent<Transform2D>(e);
        t->Position        = pos;
        t->Scale           = size;
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                                  .Texture = tex,
                                                  .Sampler = samp,
                                                  .UV      = uv,
                                                  .Layer   = layer,
                                              });

        world.AddComponent<Lifetime>(e, Lifetime{
                                            .Remaining = 5.5f,
                                        });

        return e;
    }

    // Helper: create one light entity.
    Entity MakeLightEntity(const Vec2 pos, const f32 radius, const f32 intensity, const Color color)
    {
        auto&        world                           = GetScene().GetWorld();
        const Entity e                               = world.CreateEntity();
        world.GetComponent<Transform2D>(e)->Position = pos;
        world.AddComponent<LightEmitter>(e, LightEmitter{
                                                .Radius     = radius,
                                                .Intensity  = intensity,
                                                .LightColor = color,
                                            });
        return e;
    }

    // Returns the world-space size of an entire sprite sheet texture given its ppu.
    static Vec2 SheetWorldSize(const SpriteSheet& sheet)
    {
        const Vec2 px = sheet.TexturePixelSize();
        return sheet.ToWorldSize(px.X, px.Y, TileSize);
    }

    void CreateFloor()
    {
        // Floor uses explicit layout size — it's a background quad, not a game sprite.
        MakeSpriteEntity({ 0.0f, 0.0f }, { 1920.0f, 1080.0f }, _floorTex, _floorSampler, -1, UVRect{ 0.0f, 0.0f, 4.0f, 4.0f });
    }

    void CreateTrees()
    {
        const Vec2     treeSize     = SheetWorldSize(_treeSheet);
        constexpr Vec2 positions[4] = {
            { -800.0f, -380.0f },
            {  800.0f, -380.0f },
            { -800.0f,  380.0f },
            {  800.0f,  380.0f },
        };
        for (const auto& pos : positions)
            MakeSpriteEntity(pos, treeSize, _treeSheet.GetTexture(), _sampler, 0);
    }

    void CreatePlayerTiles()
    {
        if (!_playerSheet.IsValid())
            return;

        const Vec2    tileSize  = _playerSheet.TileWorldSize(TileSize);
        const float   step      = tileSize.X;
        constexpr int TileCount = 12;
        const float   startX    = -(TileCount - 1) * step * 0.5f;
        const u32     cols      = _playerSheet.TileColumns();

        for (int i = 0; i < TileCount; ++i)
        {
            const Vec2   pos = { startX + static_cast<float>(i) * step, -270.0f };
            const Entity e   = MakeSpriteEntity(pos, tileSize, _playerSheet.GetTexture(), _sampler, 1);

            if (cols > 0)
                if (auto uv = _playerSheet.GetTile(static_cast<u32>(i) % cols, 0))
                    GetScene().GetWorld().GetComponent<SpriteRenderer>(e)->UV = *uv;
        }
    }

    void CreateSpriteBand(const SpriteSheet& sheet, const float yPos, const i32 layer, const int count)
    {
        const Vec2  spriteSize = SheetWorldSize(sheet);
        const float step       = spriteSize.X;
        const float startX     = -static_cast<float>(count / 2) * step + step * 0.5f;

        for (int i = 0; i < count; ++i)
        {
            const Vec2 pos = { startX + static_cast<float>(i) * step, yPos };
            MakeSpriteEntity(pos, spriteSize, sheet.GetTexture(), _sampler, layer);
        }
    }

    void CreateDyingSpriteBand(const SpriteSheet& sheet, const float yPos, const i32 layer, const int count)
    {
        const Vec2  spriteSize = SheetWorldSize(sheet);
        const float step       = spriteSize.X;
        const float startX     = -static_cast<float>(count / 2) * step + step * 0.5f;

        for (int i = 0; i < count; ++i)
        {
            const Vec2 pos = { startX + static_cast<float>(i) * step, yPos };
            MakeDyingSpriteEntity(pos, spriteSize, sheet.GetTexture(), _sampler, layer);
        }
    }

    void CreateLights()
    {
        // Static world lights.
        MakeLightEntity({ 0.0f, 0.0f }, 576.0f, 1.5f, Color::NaturalLight());
        MakeLightEntity({ 864.0f, -162.0f }, 192.0f, 1.5f, Color::Green());
        MakeLightEntity({ -864.0f, 324.0f }, 384.0f, 1.9f, Color::Red());

        // Random scattered lights.
        for (int i = 0; i < NumRandomLights; ++i)
        {
            const Vec2  pos       = { static_cast<float>(rand() % 1800 - 900), static_cast<float>(rand() % 1000 - 500) };
            const f32   radius    = static_cast<float>(rand() % 300) + 100.0f;
            const f32   intensity = static_cast<float>(rand() % 100) / 100.0f + 0.5f;
            const Color color     = { static_cast<float>(rand() % 100) / 100.0f, static_cast<float>(rand() % 100) / 100.0f,
                                      static_cast<float>(rand() % 100) / 100.0f, 1.0f };
            MakeLightEntity(pos, radius, intensity, color);
        }

        // Mouse-following light — stored so OnUpdate can move it.
        _mouseLightEntity = MakeLightEntity({}, 300.0f, 2.0f, Color{ 0.9f, 0.85f, 0.6f, 1.0f });
    }

    void CreateEntities()
    {
        LOG_DEBUG(Game, "Creating ECS entities...");
        CreateFloor();
        CreateTrees();
        CreatePlayerTiles();
        CreateDyingSpriteBand(_skeletonSheet, -144.0f, 1, SpriteCount);
        CreateSpriteBand(_slimeSheet, 16.0f, 5, SpriteCount);
        CreateSpriteBand(_chickenSheet, 144.0f, 2, SpriteCount);
        CreateSpriteBand(_pigSheet, 304.0f, 2, SpriteCount);
        CreateLights();
        LOG_DEBUG(Game, "ECS entities created");
    }

    // -----------------------------------------------------------------------
    // Per-frame helpers
    // -----------------------------------------------------------------------

    void UpdateMouseLight()
    {
        if (!GetScene().GetWorld().IsValid(_mouseLightEntity))
            return;
        i32 mouseX = 0, mouseY = 0;
        GetInput().GetMousePosition(mouseX, mouseY);

        const f32  actualW  = static_cast<f32>(GetWindowWidth());
        const f32  actualH  = static_cast<f32>(GetWindowHeight());
        const Vec2 mouseRef = { static_cast<f32>(mouseX) * ViewportW / actualW, static_cast<f32>(mouseY) * ViewportH / actualH };
        const Vec2 mouseWorld = GetScene().GetCamera().ScreenToWorld(mouseRef, { ViewportW, ViewportH });

        GetScene().GetWorld().GetComponent<Transform2D>(_mouseLightEntity)->Position = mouseWorld;
    }

    void SubmitDebugGrid(FramePacket& packet, const Vec2 viewport) const
    {
        const Camera2D& cam    = const_cast<ArcbitGame*>(this)->GetScene().GetCamera();
        const Vec2      camPos = cam.GetEffectivePosition();
        const f32       halfW  = (viewport.X / cam.Zoom) * 0.5f;
        const f32       halfH  = (viewport.Y / cam.Zoom) * 0.5f;

        const auto firstCol = static_cast<i32>(std::floor((camPos.X - halfW) / TileSize)) - 1;
        const auto lastCol  = static_cast<i32>(std::ceil((camPos.X + halfW) / TileSize)) + 1;
        const auto firstRow = static_cast<i32>(std::floor((camPos.Y - halfH) / TileSize)) - 1;
        const auto lastRow  = static_cast<i32>(std::ceil((camPos.Y + halfH) / TileSize)) + 1;

        const f32       totalH    = static_cast<f32>(lastRow - firstRow) * TileSize;
        const f32       totalW    = static_cast<f32>(lastCol - firstCol) * TileSize;
        const f32       originX   = static_cast<f32>(firstCol) * TileSize;
        const f32       originY   = static_cast<f32>(firstRow) * TileSize;
        const f32       thickness = std::max(1.0f, 1.0f / cam.Zoom);
        constexpr Color GridColor = { 1.0f, 1.0f, 1.0f, 0.25f };
        constexpr i32   GridLayer = 999;

        for (i32 col = firstCol; col <= lastCol; ++col)
        {
            Sprite s{};
            s.Texture  = _gridTex;
            s.Sampler  = _gridSampler;
            s.Position = { col * TileSize, originY + totalH * 0.5f };
            s.Size     = { thickness, totalH };
            s.Tint     = GridColor;
            s.Layer    = GridLayer;
            packet.Sprites.push_back(s);
        }

        for (i32 row = firstRow; row <= lastRow; ++row)
        {
            Sprite s{};
            s.Texture  = _gridTex;
            s.Sampler  = _gridSampler;
            s.Position = { originX + totalW * 0.5f, row * TileSize };
            s.Size     = { totalW, thickness };
            s.Tint     = GridColor;
            s.Layer    = GridLayer;
            packet.Sprites.push_back(s);
        }
    }

private:
    static constexpr ActionID ActionToggleGrid = MakeAction("Debug_ToggleGrid");
    static constexpr ActionID ActionMoveLeft   = MakeAction("Move_Left");
    static constexpr ActionID ActionMoveRight  = MakeAction("Move_Right");
    static constexpr ActionID ActionMoveUp     = MakeAction("Move_Up");
    static constexpr ActionID ActionMoveDown   = MakeAction("Move_Down");
    static constexpr ActionID ActionInteract   = MakeAction("Interact");
    static constexpr ActionID ActionSprint     = MakeAction("Sprint");

    // Design resolution in world pixels. The renderer exposes exactly this
    // much of the world at zoom 1 — resizing scales without revealing more world.
    static constexpr float ViewportW = 1920.0f;
    static constexpr float ViewportH = 1080.0f;
    static constexpr float TileSize  = 96.0f;

    static constexpr int SpriteCount     = 10000;
    static constexpr int NumRandomLights = 10;

    bool   _showGrid         = false;
    Entity _mouseLightEntity = Entity::Invalid();

    // Debug grid GPU resources.
    TextureHandle _gridTex;
    SamplerHandle _gridSampler;

    // Samplers.
    SamplerHandle _sampler;
    SamplerHandle _floorSampler;

    // Plain textures (no sprite structure).
    TextureHandle _floorTex;

    // Sprite assets — SpriteSheet wraps texture + JSON metadata.
    SpriteSheet _skeletonSheet;
    SpriteSheet _slimeSheet;
    SpriteSheet _chickenSheet;
    SpriteSheet _pigSheet;
    SpriteSheet _treeSheet;
    SpriteSheet _playerSheet;
};

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int /*argc*/, char* /*argv*/[])
{
    ArcbitGame game;
    game.Run();
    return EXIT_SUCCESS;
}
