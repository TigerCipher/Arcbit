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
        m_FloorTex = GetTextures().Load("assets/textures/floor.jpg");
        ARCBIT_ASSERT(m_FloorTex.IsValid(), "Failed to load floor texture");

        m_SkeletonTex = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Enemies/Skeleton.png");
        m_SlimeTex    = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Enemies/Slime_Green.png");
        m_ChickenTex  = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Animals/Chicken/Chicken.png");
        m_PigTex      = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Animals/Pig/Pig.png");
        m_TreeTex     = GetTextures().Load("assets/textures/Cute_Fantasy_Free/Outdoor decoration/Oak_Tree.png");

        ARCBIT_ASSERT(m_SkeletonTex.IsValid(), "Failed to load skeleton texture");
        ARCBIT_ASSERT(m_SlimeTex.IsValid(), "Failed to load slime texture");
        ARCBIT_ASSERT(m_ChickenTex.IsValid(), "Failed to load chicken texture");
        ARCBIT_ASSERT(m_PigTex.IsValid(), "Failed to load pig texture");
        ARCBIT_ASSERT(m_TreeTex.IsValid(), "Failed to load tree texture");

        // --- SpriteSheet ---
        m_PlayerSheet = Arcbit::SpriteSheet::Load("assets/spritesheets/player.json", GetTextures());
        if (m_PlayerSheet.IsValid())
        {
            LOG_INFO(Game, "Player sheet loaded - {}x{} grid, {} tiles", m_PlayerSheet.TileColumns(), m_PlayerSheet.TileRows(),
                     m_PlayerSheet.TileCount());

            // Demonstrate both GetTile overloads.
            if (auto uv = m_PlayerSheet.GetTile(3)) // by linear index
                LOG_DEBUG(Game, "GetTile(3)   - u0={:.3f}", uv->U0);
            if (auto uv = m_PlayerSheet.GetTile(1, 0)) // by (column, row)
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
        m_Sampler             = GetDevice().CreateSampler(nearestDesc);
        ARCBIT_ASSERT(m_Sampler.IsValid(), "Failed to create sampler");

        // Linear-repeat: floor tile blends smoothly when tiled and scaled.
        Arcbit::SamplerDesc linearDesc{};
        linearDesc.MinFilter = Arcbit::Filter::Linear;
        linearDesc.MagFilter = Arcbit::Filter::Linear;
        linearDesc.AddressU  = Arcbit::AddressMode::Repeat;
        linearDesc.AddressV  = Arcbit::AddressMode::Repeat;
        linearDesc.DebugName = "LinearRepeat";
        m_FloorSampler       = GetDevice().CreateSampler(linearDesc);
        ARCBIT_ASSERT(m_FloorSampler.IsValid(), "Failed to create floor sampler");

        // --- Random lights — world-space pixels (scattered across ±900 x ±500) ---
        for (int i = 0; i < NumRandomLights; ++i)
        {
            m_Lights[i].Position  = { static_cast<float>(rand() % 1800 - 900), static_cast<float>(rand() % 1000 - 500) };
            m_Lights[i].Radius    = static_cast<float>(rand() % 300) + 100.0f; // 100–400 px
            m_Lights[i].Intensity = static_cast<float>(rand() % 100) / 100.0f + 0.5f;
            m_Lights[i].LightColor =
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
        m_Camera.Update(dt);

        // WASD pans the camera at CameraPanSpeed world-pixels/second.
        constexpr float CameraPanSpeed = 400.0f;
        float sprintMultiplier = 1.0f;
        
        if (GetInput().IsPressed(ActionSprint))
            sprintMultiplier = 3.0f;

        const float camSpeed = CameraPanSpeed * sprintMultiplier;
        
        if (GetInput().IsPressed(ActionMoveLeft))
            m_Camera.Position.X -= camSpeed * dt;
        if (GetInput().IsPressed(ActionMoveRight))
            m_Camera.Position.X += camSpeed * dt;
        if (GetInput().IsPressed(ActionMoveUp))
            m_Camera.Position.Y -= camSpeed * dt;
        if (GetInput().IsPressed(ActionMoveDown))
            m_Camera.Position.Y += camSpeed * dt;

        // Interact adds a burst of screen shake.
        if (GetInput().JustPressed(ActionInteract))
            m_Camera.AddTrauma(0.6f);
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
        packet.CameraPosition = m_Camera.GetEffectivePosition();
        packet.CameraZoom     = m_Camera.Zoom;

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
                m_Camera.ScreenToWorld({ static_cast<float>(mouseX), static_cast<float>(mouseY) }, { ViewportW, ViewportH });

            Arcbit::PointLight light{};
            light.Position   = mouseWorld;
            light.Radius     = 300.0f;
            light.Intensity  = 2.0f;
            light.LightColor = Arcbit::Color{ 0.9f, 0.85f, 0.6f, 1.0f }; // warm lantern
            packet.Lights.push_back(light);
        }

        for (int i = 0; i < NumRandomLights; ++i)
            packet.Lights.push_back(m_Lights[i]);

        packet.Sprites.reserve(m_Sprites.size());
        packet.Sprites.assign(m_Sprites.begin(), m_Sprites.end());
    }

    // -----------------------------------------------------------------------
    // OnShutdown — destroy GPU resources.
    // The sprite pipeline is owned by RenderThread — only samplers need
    // explicit cleanup here.
    // -----------------------------------------------------------------------
    void OnShutdown() override
    {
        GetDevice().DestroySampler(m_FloorSampler);
        GetDevice().DestroySampler(m_Sampler);
    }

private:
    void CreateSprites()
    {
        LOG_DEBUG(Game, "Creating sprites...");
        // --- Floor — full screen, layer -1 (1 sprite → 1 batch) ---
        {
            Arcbit::Sprite s{};
            s.Texture  = m_FloorTex;
            s.Sampler  = m_FloorSampler;
            s.Position = { 0.0f, 0.0f };
            s.Size     = { 1920.0f, 1080.0f };
            s.UV       = { 0.0f, 0.0f, 4.0f, 4.0f }; // tile 4× across screen
            s.Layer    = -1;
            m_Sprites.push_back(s);
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
            s.Texture  = m_TreeTex;
            s.Sampler  = m_Sampler;
            s.Position = pos;
            s.Size     = { TreeSize, TreeSize };
            s.Layer    = 0;
            m_Sprites.push_back(s);
        }

        // --- Player tiles — 12 across the top row, layer 1 (12 sprites → 1 batch) ---
        // Uses GetTile(col, 0) to cycle through the spritesheet columns.
        if (m_PlayerSheet.IsValid())
        {
            constexpr float   TileSize  = 80.0f;
            constexpr int     TileCount = 12;
            constexpr float   startX    = -(TileCount - 1) * TileSize * 0.5f;
            const Arcbit::u32 cols      = m_PlayerSheet.TileColumns();

            for (int i = 0; i < TileCount; ++i)
            {
                Arcbit::Sprite s{};
                s.Texture  = m_PlayerSheet.GetTexture();
                s.Sampler  = m_Sampler;
                s.Position = { startX + static_cast<float>(i) * TileSize, -270.0f };
                s.Size     = { TileSize, TileSize };
                if (cols > 0)
                    if (auto uv = m_PlayerSheet.GetTile(static_cast<Arcbit::u32>(i) % cols, 0))
                        s.UV = *uv;
                s.Layer = 1;
                m_Sprites.push_back(s);
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
                s.Texture  = m_SkeletonTex;
                s.Sampler  = m_Sampler;
                s.Position = { startX + static_cast<float>(i) * spriteSize, -150.0f };
                s.Size     = { spriteSize, spriteSize };
                s.Layer    = 1;
                m_Sprites.push_back(s);
            }
        }

        // --- Slimes — 100 across, layer 5 (100 sprites → 1 batch) ---
        {
            constexpr float startX = -(SpriteCount - 1) * SpriteSize * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                float          spriteSize = SpriteSize * 3;
                Arcbit::Sprite s{};
                s.Texture  = m_SlimeTex;
                s.Sampler  = m_Sampler;
                s.Position = { startX + static_cast<float>(i) * spriteSize, 0.0f };
                s.Size     = { spriteSize, spriteSize };
                s.Layer    = 5;
                m_Sprites.push_back(s);
            }
        }

        // --- Chickens — 100 across, layer 2 (100 sprites → 1 batch) ---
        {
            constexpr float startX = -(SpriteCount - 1) * SpriteSize * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                Arcbit::Sprite s{};
                s.Texture  = m_ChickenTex;
                s.Sampler  = m_Sampler;
                s.Position = { startX + i * SpriteSize, 150.0f };
                s.Size     = { SpriteSize, SpriteSize };
                s.Layer    = 2;
                m_Sprites.push_back(s);
            }
        }

        // --- Pigs — 100 across, layer 2 (100 sprites → 1 batch) ---
        {
            constexpr float startX = -(SpriteCount - 1) * SpriteSize * 0.5f;
            for (int i = 0; i < SpriteCount; ++i)
            {
                Arcbit::Sprite s{};
                s.Texture  = m_PigTex;
                s.Sampler  = m_Sampler;
                s.Position = { startX + i * SpriteSize, 300.0f };
                s.Size     = { SpriteSize, SpriteSize };
                s.Layer    = 2;
                m_Sprites.push_back(s);
            }
        }

        LOG_DEBUG(Game, "Created {} sprites", m_Sprites.size());
    }

private:
    static constexpr Arcbit::ActionID ActionMoveLeft  = Arcbit::MakeAction("Move_Left");
    static constexpr Arcbit::ActionID ActionMoveRight = Arcbit::MakeAction("Move_Right");
    static constexpr Arcbit::ActionID ActionMoveUp    = Arcbit::MakeAction("Move_Up");
    static constexpr Arcbit::ActionID ActionMoveDown  = Arcbit::MakeAction("Move_Down");
    static constexpr Arcbit::ActionID ActionInteract  = Arcbit::MakeAction("Interact");
    static constexpr Arcbit::ActionID ActionSprint  = Arcbit::MakeAction("Sprint");

    // Must match the window dimensions set in the constructor.
    static constexpr float ViewportW = 1920.0f;
    static constexpr float ViewportH = 1080.0f;

    Arcbit::Camera2D m_Camera;

    // Samplers — still game-owned (control filtering per sprite).
    // The sprite pipeline itself is owned by RenderThread.
    Arcbit::SamplerHandle m_Sampler;
    Arcbit::SamplerHandle m_FloorSampler;

    // Textures — owned by TextureManager, released automatically.
    Arcbit::TextureHandle m_FloorTex;
    Arcbit::TextureHandle m_SkeletonTex;
    Arcbit::TextureHandle m_SlimeTex;
    Arcbit::TextureHandle m_ChickenTex;
    Arcbit::TextureHandle m_PigTex;
    Arcbit::TextureHandle m_TreeTex;
    Arcbit::SpriteSheet   m_PlayerSheet;

    static constexpr Arcbit::isize                  NumRandomLights = 10;
    std::array<Arcbit::PointLight, NumRandomLights> m_Lights        = {};

    std::vector<Arcbit::Sprite> m_Sprites{};
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
