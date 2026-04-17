#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/RenderTypes.h>

// ---------------------------------------------------------------------------
// ArcbitGame — demo Application subclass
//
// Phase 14: sprite batcher demo — world-space sprites, multi-texture, multi-layer.
// Phase 15: Camera2D demo — WASD pan, scroll-wheel zoom, mouse-following point light.
//           ScreenToWorld() converts mouse pixels → world position for the light.
// ---------------------------------------------------------------------------
class ArcbitGame : public Arcbit::Application
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
        GetInput().RegisterAction(ActionMoveLeft, "Move_Left");
        GetInput().RegisterAction(ActionMoveRight, "Move_Right");
        GetInput().RegisterAction(ActionMoveUp, "Move_Up");
        GetInput().RegisterAction(ActionMoveDown, "Move_Down");
        GetInput().RegisterAction(ActionInteract, "Interact");
        GetInput().RegisterAction(ActionSprint, "Sprint");

        GetInput().BindKey(ActionMoveLeft, Arcbit::Key::A);
        GetInput().BindKey(ActionMoveLeft, Arcbit::Key::Left);
        GetInput().BindKey(ActionMoveRight, Arcbit::Key::D);
        GetInput().BindKey(ActionMoveRight, Arcbit::Key::Right);
        GetInput().BindKey(ActionMoveUp, Arcbit::Key::W);
        GetInput().BindKey(ActionMoveUp, Arcbit::Key::Up);
        GetInput().BindKey(ActionMoveDown, Arcbit::Key::S);
        GetInput().BindKey(ActionMoveDown, Arcbit::Key::Down);
        GetInput().BindKey(ActionInteract, Arcbit::Key::E);
        GetInput().BindKey(ActionInteract, Arcbit::Key::Enter);
        GetInput().BindKey(ActionSprint, Arcbit::Key::LeftShift);

        GetInput().BindGamepadButton(ActionInteract, Arcbit::GamepadButton::South);
        GetInput().BindGamepadButton(ActionSprint, Arcbit::GamepadButton::RightShoulder);
        GetInput().BindGamepadAxis(ActionMoveLeft, Arcbit::GamepadAxis::LeftX);
        GetInput().BindGamepadAxis(ActionMoveRight, Arcbit::GamepadAxis::LeftX);
        GetInput().BindGamepadAxis(ActionMoveUp, Arcbit::GamepadAxis::LeftY);
        GetInput().BindGamepadAxis(ActionMoveDown, Arcbit::GamepadAxis::LeftY);

        // --- Textures ---
        _floorTex = GetTextures().Load("assets/textures/floor.jpg");
        ARCBIT_ASSERT(_floorTex.IsValid(), "Failed to load floor texture");

        _skeletonTex = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Enemies/Skeleton.png");
        _slimeTex    = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Enemies/Slime_Green.png");
        _chickenTex  = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Animals/Chicken/Chicken.png");
        _pigTex      = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Animals/Pig/Pig.png");
        _treeTex     = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Outdoor decoration/Oak_Tree.png");

        ARCBIT_ASSERT(_skeletonTex.IsValid(), "Failed to load skeleton texture");
        ARCBIT_ASSERT(_slimeTex.IsValid(), "Failed to load slime texture");
        ARCBIT_ASSERT(_chickenTex.IsValid(), "Failed to load chicken texture");
        ARCBIT_ASSERT(_pigTex.IsValid(), "Failed to load pig texture");
        ARCBIT_ASSERT(_treeTex.IsValid(), "Failed to load tree texture");

        // --- SpriteSheet ---
        _playerSheet = Arcbit::SpriteSheet::Load("assets/spritesheets/player.json", GetTextures());
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
        Arcbit::SamplerDesc nearestDesc{};
        nearestDesc.MinFilter = Arcbit::Filter::Nearest;
        nearestDesc.MagFilter = Arcbit::Filter::Nearest;
        nearestDesc.AddressU  = Arcbit::AddressMode::Repeat;
        nearestDesc.AddressV  = Arcbit::AddressMode::Repeat;
        nearestDesc.DebugName = "NearestRepeat";
        _sampler              = GetDevice().CreateSampler(nearestDesc);
        ARCBIT_ASSERT(_sampler.IsValid(), "Failed to create sampler");

        // Linear-repeat: floor tile blends smoothly when tiled and scaled.
        Arcbit::SamplerDesc linearDesc{};
        linearDesc.MinFilter = Arcbit::Filter::Linear;
        linearDesc.MagFilter = Arcbit::Filter::Linear;
        linearDesc.AddressU  = Arcbit::AddressMode::Repeat;
        linearDesc.AddressV  = Arcbit::AddressMode::Repeat;
        linearDesc.DebugName = "LinearRepeat";
        _floorSampler        = GetDevice().CreateSampler(linearDesc);
        ARCBIT_ASSERT(_floorSampler.IsValid(), "Failed to create floor sampler");

        // --- Random lights — world-space pixels (scattered across ±900 x ±500) ---
        for (int i = 0; i < NumRandomLights; ++i)
        {
            _lights[i].Position  = { static_cast<float>(rand() % 1800 - 900), static_cast<float>(rand() % 1000 - 500) };
            _lights[i].Radius    = static_cast<float>(rand() % 300) + 100.0f; // 100–400 px
            _lights[i].Intensity = static_cast<float>(rand() % 100) / 100.0f + 0.5f;
            _lights[i].LightColor =
                Arcbit::Color{ static_cast<float>(rand() % 100) / 100.0f, static_cast<float>(rand() % 100) / 100.0f,
                               static_cast<float>(rand() % 100) / 100.0f, 1.0f };
        }

        CreateSprites();
    }

    // -----------------------------------------------------------------------
    // OnUpdate — camera pan, zoom, and shake demo.
    // -----------------------------------------------------------------------
    void OnUpdate(Arcbit::f32 dt) override
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
    void OnRender(Arcbit::FramePacket& packet) override
    {
        packet.AmbientColor   = Arcbit::Color{ 0.1f, 0.1f, 0.15f, 1.0f };
        packet.CameraPosition = _camera.GetEffectivePosition();
        packet.CameraZoom     = _camera.Zoom;
        packet.CameraRotation = _camera.Rotation;

        // --- Static world lights (world-space pixels) ---
        {
            Arcbit::PointLight light{};
            light.Position   = { 0.0f, 0.0f };
            light.Radius     = 576.0f; // ≈ 60% of half-width
            light.Intensity  = 1.5f;
            light.LightColor = Arcbit::Color::NaturalLight();
            packet.Lights.push_back(light);
        }
        {
            Arcbit::PointLight light{};
            light.Position   = { 864.0f, -162.0f };
            light.Radius     = 192.0f;
            light.Intensity  = 1.5f;
            light.LightColor = Arcbit::Color::Green();
            packet.Lights.push_back(light);
        }
        {
            Arcbit::PointLight light{};
            light.Position   = { -864.0f, 324.0f };
            light.Radius     = 384.0f;
            light.Intensity  = 1.9f;
            light.LightColor = Arcbit::Color::Red();
            packet.Lights.push_back(light);
        }

        // --- Mouse-following light — converts screen pixels → world-space ---
        {
            Arcbit::i32 mouseX = 0, mouseY = 0;
            GetInput().GetMousePosition(mouseX, mouseY);
            const Arcbit::Vec2 mouseWorld =
                _camera.ScreenToWorld({ static_cast<float>(mouseX), static_cast<float>(mouseY) }, { ViewportW, ViewportH });

            Arcbit::PointLight light{};
            light.Position   = mouseWorld;
            light.Radius     = 300.0f;
            light.Intensity  = 2.0f;
            light.LightColor = Arcbit::Color{ 0.9f, 0.85f, 0.6f, 1.0f }; // warm lantern
            packet.Lights.push_back(light);
        }

        for (int i = 0; i < NumRandomLights; ++i)
            packet.Lights.push_back(_lights[i]);

        packet.Sprites.reserve(_sprites.size());
        packet.Sprites.assign(_sprites.begin(), _sprites.end());
    }

    // -----------------------------------------------------------------------
    // OnShutdown — destroy GPU resources.
    // The sprite pipeline is owned by RenderThread — only samplers need
    // explicit cleanup here.
    // -----------------------------------------------------------------------
    void OnShutdown() override
    {
        GetDevice().DestroySampler(_floorSampler);
        GetDevice().DestroySampler(_sampler);
    }

private:
    void CreateSprites()
    {
        LOG_DEBUG(Game, "Creating sprites...");
        // --- Floor — full screen, layer -1 (1 sprite → 1 batch) ---
        {
            Arcbit::Sprite s{};
            s.Texture  = _floorTex;
            s.Sampler  = _floorSampler;
            s.Position = { 0.0f, 0.0f };
            s.Size     = { 1920.0f, 1080.0f };
            s.UV       = { 0.0f, 0.0f, 4.0f, 4.0f }; // tile 4× across screen
            s.Layer    = -1;
            _sprites.push_back(s);
        }

        // --- Oak trees — 4 corners, layer 0 (4 sprites → 1 batch) ---
        constexpr Arcbit::Vec2 treePositions[4] = {
            { -800.0f, -380.0f },
            {  800.0f, -380.0f },
            { -800.0f,  380.0f },
            {  800.0f,  380.0f },
        };
        for (const auto& pos : treePositions)
        {
            constexpr float TreeSize = 192.0f;
            Arcbit::Sprite  s{};
            s.Texture  = _treeTex;
            s.Sampler  = _sampler;
            s.Position = pos;
            s.Size     = { TreeSize, TreeSize };
            s.Layer    = 0;
            _sprites.push_back(s);
        }

        // --- Player tiles — 12 across the top row, layer 1 (12 sprites → 1 batch) ---
        // Uses GetTile(col, 0) to cycle through the spritesheet columns.
        if (_playerSheet.IsValid())
        {
            constexpr float   TileSize  = 80.0f;
            constexpr int     TileCount = 12;
            constexpr float   startX    = -(TileCount - 1) * TileSize * 0.5f;
            const Arcbit::u32 cols      = _playerSheet.TileColumns();

            for (int i = 0; i < TileCount; ++i)
            {
                Arcbit::Sprite s{};
                s.Texture  = _playerSheet.GetTexture();
                s.Sampler  = _sampler;
                s.Position = { startX + static_cast<float>(i) * TileSize, -270.0f };
                s.Size     = { TileSize, TileSize };
                if (cols > 0)
                    if (auto uv = _playerSheet.GetTile(static_cast<Arcbit::u32>(i) % cols, 0))
                        s.UV = *uv;
                s.Layer = 1;
                _sprites.push_back(s);
            }
        }

        constexpr float SpriteSize  = 180.0f;
        constexpr int   SpriteCount = 10000;
        // --- Skeletons — 100 across, layer 1 (100 sprites → 1 batch) ---
        {
            constexpr float startX = -(SpriteCount - 1) * SpriteSize * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                float          spriteSize = SpriteSize * 3;
                Arcbit::Sprite s{};
                s.Texture  = _skeletonTex;
                s.Sampler  = _sampler;
                s.Position = { startX + static_cast<float>(i) * spriteSize, -150.0f };
                s.Size     = { spriteSize, spriteSize };
                s.Layer    = 1;
                _sprites.push_back(s);
            }
        }

        // --- Slimes — 100 across, layer 5 (100 sprites → 1 batch) ---
        {
            constexpr float startX = -(SpriteCount - 1) * SpriteSize * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                float          spriteSize = SpriteSize * 3;
                Arcbit::Sprite s{};
                s.Texture  = _slimeTex;
                s.Sampler  = _sampler;
                s.Position = { startX + static_cast<float>(i) * spriteSize, 0.0f };
                s.Size     = { spriteSize, spriteSize };
                s.Layer    = 5;
                _sprites.push_back(s);
            }
        }

        // --- Chickens — 100 across, layer 2 (100 sprites → 1 batch) ---
        {
            constexpr float startX = -(SpriteCount - 1) * SpriteSize * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                Arcbit::Sprite s{};
                s.Texture  = _chickenTex;
                s.Sampler  = _sampler;
                s.Position = { startX + i * SpriteSize, 150.0f };
                s.Size     = { SpriteSize, SpriteSize };
                s.Layer    = 2;
                _sprites.push_back(s);
            }
        }

        // --- Pigs — 100 across, layer 2 (100 sprites → 1 batch) ---
        {
            constexpr float startX = -(SpriteCount - 1) * SpriteSize * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                Arcbit::Sprite s{};
                s.Texture  = _pigTex;
                s.Sampler  = _sampler;
                s.Position = { startX + i * SpriteSize, 300.0f };
                s.Size     = { SpriteSize, SpriteSize };
                s.Layer    = 2;
                _sprites.push_back(s);
            }
        }

        LOG_DEBUG(Game, "Created {} sprites", _sprites.size());
    }

private:
    static constexpr Arcbit::ActionID ActionMoveLeft  = Arcbit::MakeAction("Move_Left");
    static constexpr Arcbit::ActionID ActionMoveRight = Arcbit::MakeAction("Move_Right");
    static constexpr Arcbit::ActionID ActionMoveUp    = Arcbit::MakeAction("Move_Up");
    static constexpr Arcbit::ActionID ActionMoveDown  = Arcbit::MakeAction("Move_Down");
    static constexpr Arcbit::ActionID ActionInteract  = Arcbit::MakeAction("Interact");
    static constexpr Arcbit::ActionID ActionSprint    = Arcbit::MakeAction("Sprint");

    // Must match the window dimensions set in the constructor.
    static constexpr float ViewportW = 1920.0f;
    static constexpr float ViewportH = 1080.0f;

    Arcbit::Camera2D _camera;

    // Samplers — still game-owned (control filtering per sprite).
    // The sprite pipeline itself is owned by RenderThread.
    Arcbit::SamplerHandle _sampler;
    Arcbit::SamplerHandle _floorSampler;

    // Textures — owned by TextureManager, released automatically.
    Arcbit::TextureHandle _floorTex;
    Arcbit::TextureHandle _skeletonTex;
    Arcbit::TextureHandle _slimeTex;
    Arcbit::TextureHandle _chickenTex;
    Arcbit::TextureHandle _pigTex;
    Arcbit::TextureHandle _treeTex;
    Arcbit::SpriteSheet   _playerSheet;

    static constexpr Arcbit::isize                  NumRandomLights = 10;
    std::array<Arcbit::PointLight, NumRandomLights> _lights         = {};

    std::vector<Arcbit::Sprite> _sprites{};
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
