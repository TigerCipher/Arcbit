#include <arcbit/app/Application.h>
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

// Generate a checkerboard texture (magenta / white) for the demo.
static std::vector<Arcbit::u8> MakeCheckerboard(Arcbit::u32 tileSize = 32,
                                                  Arcbit::u32 tiles    = 4)
{
    const Arcbit::u32 dim = tileSize * tiles;
    std::vector<Arcbit::u8> pixels(dim * dim * 4);
    for (Arcbit::u32 y = 0; y < dim; ++y)
    {
        for (Arcbit::u32 x = 0; x < dim; ++x)
        {
            const bool checker = ((x / tileSize) ^ (y / tileSize)) & 1;
            const Arcbit::u32 idx = (y * dim + x) * 4;
            pixels[idx + 0] = checker ? 255 : 220; // R
            pixels[idx + 1] = checker ?   0 : 220; // G
            pixels[idx + 2] = checker ? 200 : 220; // B
            pixels[idx + 3] = 255;                 // A
        }
    }
    return pixels;
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
        : Application({ .Title = "Arcbit", .Width = 1280, .Height = 720 })
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

        // --- Triangle pipeline (solid color) ---
        const Arcbit::Format swapFormat = GetSwapchainFormat();

        auto vertSpv = LoadSpv("shaders/triangle.vert.spv");
        auto fragSpv = LoadSpv("shaders/triangle.frag.spv");

        Arcbit::ShaderHandle triVert = GetDevice().CreateShader({ Arcbit::ShaderStage::Vertex,   vertSpv.data(), static_cast<Arcbit::u32>(vertSpv.size()) });
        Arcbit::ShaderHandle triFrag = GetDevice().CreateShader({ Arcbit::ShaderStage::Fragment, fragSpv.data(), static_cast<Arcbit::u32>(fragSpv.size()) });

        Arcbit::PipelineDesc triDesc{};
        triDesc.VertexShader   = triVert;
        triDesc.FragmentShader = triFrag;
        triDesc.CullMode       = Arcbit::CullMode::None;
        triDesc.ColorFormat    = swapFormat;
        triDesc.DepthFormat    = Arcbit::Format::Undefined;
        triDesc.DebugName      = "TrianglePipeline";

        m_TriPipeline = GetDevice().CreatePipeline(triDesc);
        ARCBIT_ASSERT(m_TriPipeline.IsValid(), "Failed to create triangle pipeline");
        GetDevice().DestroyShader(triVert);
        GetDevice().DestroyShader(triFrag);

        // --- Quad pipeline (textured) ---
        auto quadVertSpv = LoadSpv("shaders/quad.vert.spv");
        auto quadFragSpv = LoadSpv("shaders/quad.frag.spv");

        Arcbit::ShaderHandle quadVert = GetDevice().CreateShader({ Arcbit::ShaderStage::Vertex,   quadVertSpv.data(), static_cast<Arcbit::u32>(quadVertSpv.size()) });
        Arcbit::ShaderHandle quadFrag = GetDevice().CreateShader({ Arcbit::ShaderStage::Fragment, quadFragSpv.data(), static_cast<Arcbit::u32>(quadFragSpv.size()) });

        Arcbit::PipelineDesc quadDesc{};
        quadDesc.VertexShader   = quadVert;
        quadDesc.FragmentShader = quadFrag;
        quadDesc.CullMode       = Arcbit::CullMode::None;
        quadDesc.ColorFormat    = swapFormat;
        quadDesc.DepthFormat    = Arcbit::Format::Undefined;
        quadDesc.UseTextures    = true;
        quadDesc.DebugName      = "QuadPipeline";

        m_QuadPipeline = GetDevice().CreatePipeline(quadDesc);
        ARCBIT_ASSERT(m_QuadPipeline.IsValid(), "Failed to create quad pipeline");
        GetDevice().DestroyShader(quadVert);
        GetDevice().DestroyShader(quadFrag);

        // --- Checkerboard texture ---
        constexpr Arcbit::u32 TileSize = 32;
        constexpr Arcbit::u32 Tiles    = 4;
        constexpr Arcbit::u32 Dim      = TileSize * Tiles;

        auto pixels = MakeCheckerboard(TileSize, Tiles);

        Arcbit::TextureDesc texDesc{};
        texDesc.Width     = Dim;
        texDesc.Height    = Dim;
        texDesc.Format    = Arcbit::Format::RGBA8_UNorm;
        texDesc.Usage     = Arcbit::TextureUsage::Sampled | Arcbit::TextureUsage::Transfer;
        texDesc.DebugName = "Checkerboard";

        m_CheckerTex = GetDevice().CreateTexture(texDesc);
        ARCBIT_ASSERT(m_CheckerTex.IsValid(), "Failed to create checkerboard texture");
        GetDevice().UploadTexture(m_CheckerTex, pixels.data(), static_cast<Arcbit::u64>(pixels.size()));

        // --- Sampler ---
        Arcbit::SamplerDesc sampDesc{};
        sampDesc.MinFilter = Arcbit::Filter::Nearest;
        sampDesc.MagFilter = Arcbit::Filter::Nearest;
        sampDesc.AddressU  = Arcbit::AddressMode::Repeat;
        sampDesc.AddressV  = Arcbit::AddressMode::Repeat;
        sampDesc.DebugName = "NearestRepeat";

        m_Sampler = GetDevice().CreateSampler(sampDesc);
        ARCBIT_ASSERT(m_Sampler.IsValid(), "Failed to create sampler");
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
    // OnRender — push draw calls into the frame packet.
    // -----------------------------------------------------------------------
    void OnRender(Arcbit::FramePacket& packet) override
    {
        Arcbit::DrawCall quadDraw{};
        quadDraw.Pipeline    = m_QuadPipeline;
        quadDraw.Texture     = m_CheckerTex;
        quadDraw.Sampler     = m_Sampler;
        quadDraw.VertexCount = 6;
        packet.DrawCalls.push_back(quadDraw);
    }

    // -----------------------------------------------------------------------
    // OnShutdown — destroy GPU resources before the device is torn down.
    // -----------------------------------------------------------------------
    void OnShutdown() override
    {
        GetDevice().DestroySampler(m_Sampler);
        GetDevice().DestroyTexture(m_CheckerTex);
        GetDevice().DestroyPipeline(m_QuadPipeline);
        GetDevice().DestroyPipeline(m_TriPipeline);
    }

private:
    // Action IDs — computed at compile time, no string cost at runtime.
    static constexpr Arcbit::ActionID ActionMoveLeft  = Arcbit::MakeAction("Move_Left");
    static constexpr Arcbit::ActionID ActionMoveRight = Arcbit::MakeAction("Move_Right");
    static constexpr Arcbit::ActionID ActionMoveUp    = Arcbit::MakeAction("Move_Up");
    static constexpr Arcbit::ActionID ActionMoveDown  = Arcbit::MakeAction("Move_Down");
    static constexpr Arcbit::ActionID ActionInteract  = Arcbit::MakeAction("Interact");

    // GPU resources — created in OnStart, destroyed in OnShutdown.
    Arcbit::PipelineHandle m_TriPipeline;
    Arcbit::PipelineHandle m_QuadPipeline;
    Arcbit::TextureHandle  m_CheckerTex;
    Arcbit::SamplerHandle  m_Sampler;
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
