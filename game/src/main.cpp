#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/RenderTypes.h>

#include <cmath>

using namespace Arcbit;

// ---------------------------------------------------------------------------
// ArcbitGame — demo Application subclass
//
// Phase 14: sprite batcher demo — world-space sprites, multi-texture, multi-layer.
// Phase 15: Camera2D demo — WASD pan, scroll-wheel zoom, mouse-following point light.
//           ScreenToWorld() converts mouse pixels → world position for the light.
// ---------------------------------------------------------------------------
class ArcbitGame : public Application
{
public:
    ArcbitGame() : Application({ .Title = "Arcbit", .Width = 1920, .Height = 1080 }) {}

protected:
    // -----------------------------------------------------------------------
    // OnStart — load assets, create samplers, register input.
    // -----------------------------------------------------------------------
    void OnStart() override
    {
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

        // --- Plain textures (no sprite structure) ---
        _floorTex = GetTextures().Load("assets/textures/floor.jpg");
        ARCBIT_ASSERT(_floorTex.IsValid(), "Failed to load floor texture");

        // --- Sprite assets (structured sprite sheets — will gain animations in Phase 17) ---
        _skeletonSheet = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Enemies/Skeleton.json",                GetTextures());
        _slimeSheet    = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Enemies/Slime_Green.json",             GetTextures());
        _chickenSheet  = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Animals/Chicken/Chicken.json",         GetTextures());
        _pigSheet      = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Animals/Pig/Pig.json",                 GetTextures());
        _treeSheet     = SpriteSheet::Load("assets/textures/Cute_Fantasy_Free/Outdoor decoration/Oak_Tree.json",     GetTextures());

        ARCBIT_ASSERT(_skeletonSheet.IsValid(), "Failed to load skeleton sprite");
        ARCBIT_ASSERT(_slimeSheet.IsValid(),    "Failed to load slime sprite");
        ARCBIT_ASSERT(_chickenSheet.IsValid(),  "Failed to load chicken sprite");
        ARCBIT_ASSERT(_pigSheet.IsValid(),      "Failed to load pig sprite");
        ARCBIT_ASSERT(_treeSheet.IsValid(),     "Failed to load tree sprite");

        // --- SpriteSheet ---
        _playerSheet = SpriteSheet::Load("assets/spritesheets/player.json", GetTextures());
        if (_playerSheet.IsValid())
        {
            LOG_INFO(Game, "Player sheet loaded - {}x{} grid, {} tiles", _playerSheet.TileColumns(), _playerSheet.TileRows(),
                     _playerSheet.TileCount());

            // Demonstrate both GetTile overloads.
            if (auto uv = _playerSheet.GetTile(3)) // by linear index
                LOG_DEBUG(Game, "GetTile(3)   - u0={:.3f}", uv->U0);
            if (auto uv = _playerSheet.GetTile(1, 0)) // by (column, row)
                LOG_DEBUG(Game, "GetTile(1,0) - u0={:.3f}", uv->U0);
        }

        // --- Samplers ---
        // Nearest-repeat: pixel-art sprites stay crisp when scaled.
        SamplerDesc nearestDesc{};
        nearestDesc.MinFilter = Filter::Nearest;
        nearestDesc.MagFilter = Filter::Nearest;
        nearestDesc.AddressU  = AddressMode::Repeat;
        nearestDesc.AddressV  = AddressMode::Repeat;
        nearestDesc.DebugName = "NearestRepeat";
        _sampler              = GetDevice().CreateSampler(nearestDesc);
        ARCBIT_ASSERT(_sampler.IsValid(), "Failed to create sampler");

        // Linear-repeat: floor tile blends smoothly when tiled and scaled.
        SamplerDesc linearDesc{};
        linearDesc.MinFilter = Filter::Linear;
        linearDesc.MagFilter = Filter::Linear;
        linearDesc.AddressU  = AddressMode::Repeat;
        linearDesc.AddressV  = AddressMode::Repeat;
        linearDesc.DebugName = "LinearRepeat";
        _floorSampler        = GetDevice().CreateSampler(linearDesc);
        ARCBIT_ASSERT(_floorSampler.IsValid(), "Failed to create floor sampler");

        // --- Debug grid texture — 1×1 white pixel for solid-colour sprite lines ---
        {
            TextureDesc desc{};
            desc.Width     = 1;
            desc.Height    = 1;
            desc.Format    = Format::RGBA8_UNorm;
            desc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
            desc.DebugName = "Debug_SolidWhite";
            _gridTex = GetDevice().CreateTexture(desc);

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

        // --- Random lights — world-space pixels (scattered across ±900 x ±500) ---
        for (int i = 0; i < NumRandomLights; ++i)
        {
            _lights[i].Position  = { static_cast<float>(rand() % 1800 - 900), static_cast<float>(rand() % 1000 - 500) };
            _lights[i].Radius    = static_cast<float>(rand() % 300) + 100.0f; // 100–400 px
            _lights[i].Intensity = static_cast<float>(rand() % 100) / 100.0f + 0.5f;
            _lights[i].LightColor =
                Color{ static_cast<float>(rand() % 100) / 100.0f, static_cast<float>(rand() % 100) / 100.0f,
                               static_cast<float>(rand() % 100) / 100.0f, 1.0f };
        }

        CreateSprites();
    }

    // -----------------------------------------------------------------------
    // OnUpdate — camera pan, zoom, and shake demo.
    // -----------------------------------------------------------------------
    void OnUpdate(f32 dt) override
    {
        _camera.Update(dt);

        // WASD pans the camera at CameraPanSpeed world-pixels/second.
        constexpr float CameraPanSpeed   = 400.0f;
        float           sprintMultiplier = 1.0f;

        if (GetInput().IsPressed(ActionSprint))
            sprintMultiplier = 3.0f;

        const float camSpeed = CameraPanSpeed * sprintMultiplier;

        if (GetInput().IsPressed(ActionMoveLeft))
            _camera.Position.X -= camSpeed * dt;
        if (GetInput().IsPressed(ActionMoveRight))
            _camera.Position.X += camSpeed * dt;
        if (GetInput().IsPressed(ActionMoveUp))
            _camera.Position.Y -= camSpeed * dt;
        if (GetInput().IsPressed(ActionMoveDown))
            _camera.Position.Y += camSpeed * dt;

        if (GetInput().JustPressed(ActionToggleGrid))
            _showGrid = !_showGrid;

        // Interact adds a burst of screen shake.
        if (GetInput().JustPressed(ActionInteract))
            _camera.AddTrauma(0.6f);
    }

    // -----------------------------------------------------------------------
    // OnRender — build the frame packet with sprites and lights.
    //
    // All positions are in world pixels. Camera at (0, 0) maps screen center
    // to world origin. The 1920×1080 window covers world (-960,-540)→(960,540).
    //
    // Scene layout (demonstrates batching — many sprites, few texture groups):
    //   Layer -1 : floor         — 1 sprite    → 1 batch
    //   Layer  0 : oak trees     — 4 sprites   → 1 batch
    //   Layer  1 : player tiles  — 12 sprites  → 1 batch   (same texture, different UVs)
    //   Layer  1 : skeletons     — 100 sprites → 1 batch
    //   Layer  2 : chickens      — 100 sprites → 1 batch
    //   Layer  2 : pigs          — 100 sprites → 1 batch
    //   Layer  5 : slimes        — 100 sprites → 1 batch
    //                              417 total   → 7 batches
    // -----------------------------------------------------------------------
    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor   = Color{ 0.1f, 0.1f, 0.15f, 1.0f };
        packet.CameraPosition = _camera.GetEffectivePosition();
        packet.CameraZoom     = _camera.Zoom;
        packet.CameraRotation = _camera.Rotation;
        packet.ReferenceSize  = { ViewportW, ViewportH };

        constexpr Vec2 viewport = { ViewportW, ViewportH };

        // Helper: push a light only if its circle overlaps the visible area.
        auto SubmitLight = [&](const PointLight& light)
        {
            if (_camera.IsLightVisible(light.Position, light.Radius, viewport))
                packet.Lights.push_back(light);
        };

        // --- Static world lights (world-space pixels) ---
        {
            PointLight light{};
            light.Position   = { 0.0f, 0.0f };
            light.Radius     = 576.0f;
            light.Intensity  = 1.5f;
            light.LightColor = Color::NaturalLight();
            SubmitLight(light);
        }
        {
            PointLight light{};
            light.Position   = { 864.0f, -162.0f };
            light.Radius     = 192.0f;
            light.Intensity  = 1.5f;
            light.LightColor = Color::Green();
            SubmitLight(light);
        }
        {
            PointLight light{};
            light.Position   = { -864.0f, 324.0f };
            light.Radius     = 384.0f;
            light.Intensity  = 1.9f;
            light.LightColor = Color::Red();
            SubmitLight(light);
        }

        // --- Mouse-following light — always visible (cursor is on screen) ---
        {
            i32 mouseX = 0, mouseY = 0;
            GetInput().GetMousePosition(mouseX, mouseY);

            // Mouse is in actual window pixels. Scale to reference pixels first so
            // ScreenToWorld maps correctly regardless of the current window size.
            const f32 actualW = static_cast<f32>(GetWindowWidth());
            const f32 actualH = static_cast<f32>(GetWindowHeight());
            const Vec2 mouseRef = {
                static_cast<f32>(mouseX) * ViewportW / actualW,
                static_cast<f32>(mouseY) * ViewportH / actualH,
            };
            const Vec2 mouseWorld = _camera.ScreenToWorld(mouseRef, viewport);

            PointLight light{};
            light.Position   = mouseWorld;
            light.Radius     = 300.0f;
            light.Intensity  = 2.0f;
            light.LightColor = Color{ 0.9f, 0.85f, 0.6f, 1.0f };
            packet.Lights.push_back(light); // no cull — cursor is always on screen
        }

        for (int i = 0; i < NumRandomLights; ++i)
            SubmitLight(_lights[i]);

        // Frustum cull sprites before handing off to the render thread.
        packet.Sprites.reserve(_sprites.size());
        for (const auto& s : _sprites)
            if (_camera.IsVisible(s.Position, s.Size, viewport))
                packet.Sprites.push_back(s);

        if (_showGrid)
            SubmitDebugGrid(packet, viewport);
    }

    // -----------------------------------------------------------------------
    // OnShutdown — destroy GPU resources.
    // The sprite pipeline is owned by RenderThread — only samplers need
    // explicit cleanup here.
    // -----------------------------------------------------------------------
    void OnShutdown() override
    {
        GetDevice().DestroyTexture(_gridTex);
        GetDevice().DestroySampler(_gridSampler);
        GetDevice().DestroySampler(_floorSampler);
        GetDevice().DestroySampler(_sampler);
    }

private:
    void SubmitDebugGrid(FramePacket& packet, Vec2 viewport) const
    {
        // Determine the visible world bounds from the camera.
        const Vec2 camPos = _camera.GetEffectivePosition();
        const f32  halfW  = (viewport.X / _camera.Zoom) * 0.5f;
        const f32  halfH  = (viewport.Y / _camera.Zoom) * 0.5f;

        // Snap to tile boundaries, adding one tile of margin so lines at the edges
        // are always fully visible even after camera rotation.
        const auto firstCol = static_cast<i32>(std::floor((camPos.X - halfW) / TileSize)) - 1;
        const auto lastCol  = static_cast<i32>(std::ceil ((camPos.X + halfW) / TileSize)) + 1;
        const auto firstRow = static_cast<i32>(std::floor((camPos.Y - halfH) / TileSize)) - 1;
        const auto lastRow  = static_cast<i32>(std::ceil ((camPos.Y + halfH) / TileSize)) + 1;

        const f32   totalH    = static_cast<f32>(lastRow  - firstRow) * TileSize;
        const f32   totalW    = static_cast<f32>(lastCol  - firstCol) * TileSize;
        const f32   originX   = static_cast<f32>(firstCol) * TileSize;
        const f32   originY   = static_cast<f32>(firstRow) * TileSize;
        const f32   thickness = std::max(1.0f, 1.0f / _camera.Zoom); // 1 screen px wide
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

    // Returns the world-space size of a full texture driven by its ppu.
    static Vec2 SheetWorldSize(const SpriteSheet& sheet, f32 worldTileSize)
    {
        const Vec2 px = sheet.TexturePixelSize();
        return sheet.ToWorldSize(px.X, px.Y, worldTileSize);
    }

    void CreateSprites()
    {
        LOG_DEBUG(Game, "Creating sprites...");

        // --- Floor — explicit layout size; not a game sprite, no ppu ---
        {
            Sprite s{};
            s.Texture  = _floorTex;
            s.Sampler  = _floorSampler;
            s.Position = { 0.0f, 0.0f };
            s.Size     = { 1920.0f, 1080.0f };
            s.UV       = { 0.0f, 0.0f, 4.0f, 4.0f };
            s.Layer    = -1;
            _sprites.push_back(s);
        }

        // --- Oak trees — 4 corners, layer 0 ---
        {
            const Vec2 treeSize = SheetWorldSize(_treeSheet, TileSize);
            constexpr Vec2 positions[4] = {
                { -800.0f, -380.0f }, {  800.0f, -380.0f },
                { -800.0f,  380.0f }, {  800.0f,  380.0f },
            };
            for (const auto& pos : positions)
            {
                Sprite s{};
                s.Texture  = _treeSheet.GetTexture();
                s.Sampler  = _sampler;
                s.Position = pos;
                s.Size     = treeSize;
                s.Layer    = 0;
                _sprites.push_back(s);
            }
        }

        // --- Player tiles — 12 across the top row, layer 1 ---
        // Each tile is one grid cell; TileWorldSize drives the size via ppu.
        if (_playerSheet.IsValid())
        {
            const Vec2  tileSize = _playerSheet.TileWorldSize(TileSize);
            const float step     = tileSize.X;
            constexpr int TileCount = 12;
            const float startX = -(TileCount - 1) * step * 0.5f;
            const u32   cols   = _playerSheet.TileColumns();

            for (int i = 0; i < TileCount; ++i)
            {
                Sprite s{};
                s.Texture  = _playerSheet.GetTexture();
                s.Sampler  = _sampler;
                s.Position = { startX + static_cast<float>(i) * step, -270.0f };
                s.Size     = tileSize;
                if (cols > 0)
                    if (auto uv = _playerSheet.GetTile(static_cast<u32>(i) % cols, 0))
                        s.UV = *uv;
                s.Layer = 1;
                _sprites.push_back(s);
            }
        }

        constexpr int SpriteCount = 10000;

        // --- Skeletons — layer 1 ---
        {
            const Vec2  spriteSize = SheetWorldSize(_skeletonSheet, TileSize);
            const float step       = spriteSize.X;
            const float startX     = -static_cast<float>(SpriteCount / 2) * step + step * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                Sprite s{};
                s.Texture  = _skeletonSheet.GetTexture();
                s.Sampler  = _sampler;
                s.Position = { startX + static_cast<float>(i) * step, -144.0f };
                s.Size     = spriteSize;
                s.Layer    = 1;
                _sprites.push_back(s);
            }
        }

        // --- Slimes — layer 5 ---
        {
            const Vec2  spriteSize = SheetWorldSize(_slimeSheet, TileSize);
            const float step       = spriteSize.X;
            const float startX     = -static_cast<float>(SpriteCount / 2) * step + step * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                Sprite s{};
                s.Texture  = _slimeSheet.GetTexture();
                s.Sampler  = _sampler;
                s.Position = { startX + static_cast<float>(i) * step, 16.0f };
                s.Size     = spriteSize;
                s.Layer    = 5;
                _sprites.push_back(s);
            }
        }

        // --- Chickens — layer 2 ---
        {
            const Vec2  spriteSize = SheetWorldSize(_chickenSheet, TileSize);
            const float step       = spriteSize.X;
            const float startX     = -static_cast<float>(SpriteCount / 2) * step;
            for (int i = 0; i < SpriteCount; ++i)
            {
                Sprite s{};
                s.Texture  = _chickenSheet.GetTexture();
                s.Sampler  = _sampler;
                s.Position = { startX + static_cast<float>(i) * step, 144.0f };
                s.Size     = spriteSize;
                s.Layer    = 2;
                _sprites.push_back(s);
            }
        }

        // --- Pigs — layer 2 ---
        {
            const Vec2  spriteSize = SheetWorldSize(_pigSheet, TileSize);
            const float step       = spriteSize.X;
            const float startX     = -static_cast<float>(SpriteCount / 2) * step + step * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                Sprite s{};
                s.Texture  = _pigSheet.GetTexture();
                s.Sampler  = _sampler;
                s.Position = { startX + static_cast<float>(i) * step, 304.0f };
                s.Size     = spriteSize;
                s.Layer    = 2;
                _sprites.push_back(s);
            }
        }

        LOG_DEBUG(Game, "Created {} sprites", _sprites.size());
    }

private:
    static constexpr ActionID ActionToggleGrid = MakeAction("Debug_ToggleGrid");
    static constexpr ActionID ActionMoveLeft  = MakeAction("Move_Left");
    static constexpr ActionID ActionMoveRight = MakeAction("Move_Right");
    static constexpr ActionID ActionMoveUp    = MakeAction("Move_Up");
    static constexpr ActionID ActionMoveDown  = MakeAction("Move_Down");
    static constexpr ActionID ActionInteract  = MakeAction("Interact");
    static constexpr ActionID ActionSprint    = MakeAction("Sprint");

    // Design (reference) resolution in world pixels. The renderer always exposes
    // exactly this much of the world at zoom 1 — resizing the window scales the
    // image without changing the visible world area.
    static constexpr float ViewportW = 1920.0f;
    static constexpr float ViewportH = 1080.0f;
    static constexpr float TileSize  = 92.0f;

    Camera2D _camera;

    // Debug grid — 1×1 white texture rendered as thin tile-boundary lines.
    bool          _showGrid   = false;
    TextureHandle _gridTex;
    SamplerHandle _gridSampler;

    // Samplers — still game-owned (control filtering per sprite).
    // The sprite pipeline itself is owned by RenderThread.
    SamplerHandle _sampler;
    SamplerHandle _floorSampler;

    // Plain textures — no sprite structure, loaded directly via TextureManager.
    TextureHandle _floorTex;

    // Sprite assets — SpriteSheet wraps the texture and its JSON metadata.
    // The underlying textures are owned by TextureManager and released automatically.
    SpriteSheet _skeletonSheet;
    SpriteSheet _slimeSheet;
    SpriteSheet _chickenSheet;
    SpriteSheet _pigSheet;
    SpriteSheet _treeSheet;
    SpriteSheet _playerSheet;

    static constexpr isize                  NumRandomLights = 10;
    std::array<PointLight, NumRandomLights> _lights         = {};

    std::vector<Sprite> _sprites{};
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
