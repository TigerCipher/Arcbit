#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/render/RenderDevice.h>
#include <arcbit/render/RenderTypes.h>

#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Load a SPIR-V binary from disk. Asserts on failure.
static std::vector<Arcbit::u8> LoadSpv(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ARCBIT_ASSERT(file.is_open(), "Failed to open shader file — check working directory");
    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<Arcbit::u8> buf(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

// ---------------------------------------------------------------------------
// ArcbitGame — demo Application subclass
//
// Renders a textured quad (checkerboard) using the quadPipeline.
// Replace the contents of each hook with real game logic in later phases.
// ---------------------------------------------------------------------------
class ArcbitGame : public Arcbit::Application
{
public:
    ArcbitGame()
        : Application({ .Title = "Arcbit", .Width = 1920, .Height = 1080 })
    {}

protected:
    // -----------------------------------------------------------------------
    // OnStart — create GPU resources and register input.
    // Called once after the engine is ready, before the game loop begins.
    // -----------------------------------------------------------------------
    void OnStart() override
    {
        // --- Input ---
        GetInput().RegisterAction(ActionMoveLeft,  "Move_Left");
        GetInput().RegisterAction(ActionMoveRight, "Move_Right");
        GetInput().RegisterAction(ActionMoveUp,    "Move_Up");
        GetInput().RegisterAction(ActionMoveDown,  "Move_Down");
        GetInput().RegisterAction(ActionInteract,  "Interact");

        // Default keyboard bindings — overwritten by settings if a save exists.
        GetInput().BindKey(ActionMoveLeft,  Arcbit::Key::A);
        GetInput().BindKey(ActionMoveLeft,  Arcbit::Key::Left);
        GetInput().BindKey(ActionMoveRight, Arcbit::Key::D);
        GetInput().BindKey(ActionMoveRight, Arcbit::Key::Right);
        GetInput().BindKey(ActionMoveUp,    Arcbit::Key::W);
        GetInput().BindKey(ActionMoveUp,    Arcbit::Key::Up);
        GetInput().BindKey(ActionMoveDown,  Arcbit::Key::S);
        GetInput().BindKey(ActionMoveDown,  Arcbit::Key::Down);
        GetInput().BindKey(ActionInteract,  Arcbit::Key::E);
        GetInput().BindKey(ActionInteract,  Arcbit::Key::Enter);

        // Gamepad bindings.
        GetInput().BindGamepadButton(ActionInteract,  Arcbit::GamepadButton::South);
        GetInput().BindGamepadAxis(ActionMoveLeft,    Arcbit::GamepadAxis::LeftX);
        GetInput().BindGamepadAxis(ActionMoveRight,   Arcbit::GamepadAxis::LeftX);
        GetInput().BindGamepadAxis(ActionMoveUp,      Arcbit::GamepadAxis::LeftY);
        GetInput().BindGamepadAxis(ActionMoveDown,    Arcbit::GamepadAxis::LeftY);

        // --- Forward+ pipeline (lit textured sprites) ---
        const Arcbit::Format swapFormat = GetSwapchainFormat();

        auto fwdVertSpv = LoadSpv("shaders/forward.vert.spv");
        auto fwdFragSpv = LoadSpv("shaders/forward.frag.spv");

        Arcbit::ShaderHandle fwdVert = GetDevice().CreateShader({ Arcbit::ShaderStage::Vertex,   fwdVertSpv.data(), static_cast<Arcbit::u32>(fwdVertSpv.size()) });
        Arcbit::ShaderHandle fwdFrag = GetDevice().CreateShader({ Arcbit::ShaderStage::Fragment, fwdFragSpv.data(), static_cast<Arcbit::u32>(fwdFragSpv.size()) });

        Arcbit::PipelineDesc fwdDesc{};
        fwdDesc.VertexShader     = fwdVert;
        fwdDesc.FragmentShader   = fwdFrag;
        fwdDesc.CullMode         = Arcbit::CullMode::None;
        fwdDesc.ColorFormat      = swapFormat;
        fwdDesc.DepthFormat      = Arcbit::Format::Undefined;
        fwdDesc.UseTextures      = true;    // set 0 = albedo
        fwdDesc.UseNormalTexture = true;    // set 1 = normal map
        fwdDesc.UseStorageBuffer = true;    // set 2 = light SSBO
        // Alpha blending for sprite transparency.
        fwdDesc.Blend.Enable   = true;
        fwdDesc.Blend.SrcColor = Arcbit::BlendFactor::SrcAlpha;
        fwdDesc.Blend.DstColor = Arcbit::BlendFactor::OneMinusSrcAlpha;
        fwdDesc.Blend.ColorOp  = Arcbit::BlendOp::Add;
        fwdDesc.Blend.SrcAlpha = Arcbit::BlendFactor::One;
        fwdDesc.Blend.DstAlpha = Arcbit::BlendFactor::Zero;
        fwdDesc.Blend.AlphaOp  = Arcbit::BlendOp::Add;
        fwdDesc.DebugName      = "ForwardPipeline";

        m_ForwardPipeline = GetDevice().CreatePipeline(fwdDesc);
        ARCBIT_ASSERT(m_ForwardPipeline.IsValid(), "Failed to create forward+ pipeline");
        GetDevice().DestroyShader(fwdVert);
        GetDevice().DestroyShader(fwdFrag);

        // --- Textures via TextureManager ---
        // Load the woods background texture. The manager caches by path, so
        // calling Load() again elsewhere returns the same handle at no cost.
        m_WoodsTex = GetTextures().Load("assets/textures/free_pixel_16_woods.png");
        ARCBIT_ASSERT(m_WoodsTex.IsValid(), "Failed to load woods texture");

        // Demonstrate SpriteSheet: load the player atlas as a tile grid.
        // The JSON metadata file defines tile_width/tile_height; the loader
        // computes UV rects for every cell automatically.
        m_PlayerSheet = Arcbit::SpriteSheet::Load(
            "assets/spritesheets/player.json", GetTextures());
        if (m_PlayerSheet.IsValid())
            LOG_INFO(Engine, "Player sheet loaded — {} tiles", m_PlayerSheet.TileCount());

        // --- Floor texture ---
        m_FloorTex = GetTextures().Load("assets/textures/floor.jpg");
        ARCBIT_ASSERT(m_FloorTex.IsValid(), "Failed to load floor texture");

        // --- Samplers ---
        // Nearest-repeat: for pixel-art sprites where texels should stay crisp.
        Arcbit::SamplerDesc sampDesc{};
        sampDesc.MinFilter = Arcbit::Filter::Nearest;
        sampDesc.MagFilter = Arcbit::Filter::Nearest;
        sampDesc.AddressU  = Arcbit::AddressMode::Repeat;
        sampDesc.AddressV  = Arcbit::AddressMode::Repeat;
        sampDesc.DebugName = "NearestRepeat";

        m_Sampler = GetDevice().CreateSampler(sampDesc);
        ARCBIT_ASSERT(m_Sampler.IsValid(), "Failed to create sampler");

        // Linear-repeat: for the floor tile so it blends smoothly when scaled.
        Arcbit::SamplerDesc floorSampDesc{};
        floorSampDesc.MinFilter = Arcbit::Filter::Linear;
        floorSampDesc.MagFilter = Arcbit::Filter::Linear;
        floorSampDesc.AddressU  = Arcbit::AddressMode::Repeat;
        floorSampDesc.AddressV  = Arcbit::AddressMode::Repeat;
        floorSampDesc.DebugName = "LinearRepeat";

        m_FloorSampler = GetDevice().CreateSampler(floorSampDesc);
        ARCBIT_ASSERT(m_FloorSampler.IsValid(), "Failed to create floor sampler");

        // Create random lights
        for (int i = 0; i < NumRandomLights; ++i)
        {
            m_Lights[i].Position   = { static_cast<float>(rand() % 100) / 100.0f - 0.5f, static_cast<float>(rand() % 100) / 100.0f - 0.5f };
            m_Lights[i].Radius     = static_cast<float>(rand() % 100) / 100.0f * 0.5f + 0.1f;
            m_Lights[i].Intensity  = static_cast<float>(rand() % 100) / 100.0f + 0.5f;
            m_Lights[i].LightColor = Arcbit::Color{ static_cast<float>(rand() % 100) / 100.0f, static_cast<float>(rand() % 100) / 100.0f, static_cast<float>(rand() % 100) / 100.0f, 1.0f };
        }
    }

    // -----------------------------------------------------------------------
    // OnUpdate — fixed-timestep game logic.
    // -----------------------------------------------------------------------
    void OnUpdate(Arcbit::f32 /*dt*/) override
    {
        if (GetInput().JustPressed(ActionInteract))
            LOG_DEBUG(Engine, "Interact triggered");
        if (GetInput().IsPressed(ActionMoveLeft))
            LOG_DEBUG(Engine, "Moving left  (axis={:.2f})", GetInput().AxisValue(ActionMoveLeft));
        if (GetInput().IsPressed(ActionMoveRight))
            LOG_DEBUG(Engine, "Moving right (axis={:.2f})", GetInput().AxisValue(ActionMoveRight));
        if (GetInput().IsPressed(ActionMoveUp))
            LOG_DEBUG(Engine, "Moving up    (axis={:.2f})", GetInput().AxisValue(ActionMoveUp));
        if (GetInput().IsPressed(ActionMoveDown))
            LOG_DEBUG(Engine, "Moving down  (axis={:.2f})", GetInput().AxisValue(ActionMoveDown));
    }

    // -----------------------------------------------------------------------
    // OnRender — push draw calls and lights into the frame packet.
    // -----------------------------------------------------------------------
    void OnRender(Arcbit::FramePacket& packet) override
    {
        // Ambient light — dim so point lights are clearly visible.
        packet.AmbientColor = Arcbit::Color{ 0.1f, 0.1f, 0.15f, 1.0f };

        // Test point light — warm orange, positioned at screen center.
        {
            Arcbit::PointLight light{};
            light.Position   = { 0.0f, 0.0f };
            light.Radius     = 0.6f;
            light.Intensity  = 1.5f;
            light.LightColor = Arcbit::Color::NaturalLight();
            packet.Lights.push_back(light);
        }

        // Test point light — green, positioned at right half
        {
            Arcbit::PointLight light{};
            light.Position   = { 0.9f, -0.3f };
            light.Radius     = 0.2f;
            light.Intensity  = 1.5f;
            light.LightColor = Arcbit::Color::Green();
            packet.Lights.push_back(light);
        }

        // Test point light — red, positioned at left half
        {
            Arcbit::PointLight light{};
            light.Position   = { -0.9f, 0.6f };
            light.Radius     = 0.4f;
            light.Intensity  = 1.9f;
            light.LightColor = Arcbit::Color::Red();
            packet.Lights.push_back(light);
        }

        // Now various random lights
        for (int i = 0; i < NumRandomLights; ++i)
        {
            Arcbit::PointLight light = m_Lights[i];
            packet.Lights.push_back(light);
        }

        // Floor — full screen, drawn first so sprites render on top.
        // UV scale of 4×4 tiles the 512px texture across the 1280×720 window.
        {
            Arcbit::DrawCall dc{};
            dc.Pipeline    = m_ForwardPipeline;
            dc.Texture     = m_FloorTex;
            dc.Sampler     = m_FloorSampler;
            dc.VertexCount = 6;
            dc.Position    = { 0.0f, 0.0f };
            dc.Scale       = { 1.0f, 1.0f };
            dc.UV          = { 0.0f, 0.0f, 4.0f, 4.0f }; // tile 4x
            packet.DrawCalls.push_back(dc);
        }

        // Woods texture — left half of the screen.
        {
            Arcbit::DrawCall dc{};
            dc.Pipeline    = m_ForwardPipeline;
            dc.Texture     = m_WoodsTex;
            dc.Sampler     = m_Sampler;
            dc.VertexCount = 6;
            dc.Position    = { -0.5f, 0.0f };
            dc.Scale       = {  0.5f, 1.0f };
            packet.DrawCalls.push_back(dc);
        }

        // Player tile 0 — right half of the screen.
        if (m_PlayerSheet.IsValid())
        {
            Arcbit::DrawCall dc{};
            dc.Pipeline    = m_ForwardPipeline;
            dc.Texture     = m_PlayerSheet.GetTexture();
            dc.Sampler     = m_Sampler;
            dc.VertexCount = 6;
            dc.Position    = { 0.5f, 0.0f };
            dc.Scale       = { 0.5f, 1.0f };
            if (auto uv = m_PlayerSheet.GetTile(3))
                dc.UV = *uv;
            packet.DrawCalls.push_back(dc);
        }
    }

    // -----------------------------------------------------------------------
    // OnShutdown — destroy GPU resources before the device is torn down.
    // Textures loaded via GetTextures() are released automatically by
    // Application; only manually created GPU objects need explicit cleanup.
    // -----------------------------------------------------------------------
    void OnShutdown() override
    {
        GetDevice().DestroySampler(m_FloorSampler);
        GetDevice().DestroySampler(m_Sampler);
        GetDevice().DestroyPipeline(m_ForwardPipeline);
    }

private:
    // Action IDs — computed at compile time, no string cost at runtime.
    static constexpr Arcbit::ActionID ActionMoveLeft  = Arcbit::MakeAction("Move_Left");
    static constexpr Arcbit::ActionID ActionMoveRight = Arcbit::MakeAction("Move_Right");
    static constexpr Arcbit::ActionID ActionMoveUp    = Arcbit::MakeAction("Move_Up");
    static constexpr Arcbit::ActionID ActionMoveDown  = Arcbit::MakeAction("Move_Down");
    static constexpr Arcbit::ActionID ActionInteract  = Arcbit::MakeAction("Interact");

    // GPU resources — pipelines and samplers created in OnStart, destroyed in OnShutdown.
    // Textures are owned by the TextureManager and released automatically.
    Arcbit::PipelineHandle m_ForwardPipeline;
    Arcbit::SamplerHandle  m_Sampler;
    Arcbit::SamplerHandle  m_FloorSampler;

    // Loaded via TextureManager — no manual destroy needed.
    Arcbit::TextureHandle  m_FloorTex;
    Arcbit::TextureHandle  m_WoodsTex;
    Arcbit::SpriteSheet    m_PlayerSheet;

    static constexpr Arcbit::isize NumRandomLights = 10;
    std::array<Arcbit::PointLight, NumRandomLights> m_Lights = {};
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
