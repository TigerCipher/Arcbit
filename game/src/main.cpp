#include <arcbit/arcbit.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Profiler.h>
#include <arcbit/app/Window.h>
#include <arcbit/render/RenderDevice.h>
#include <arcbit/render/RenderThread.h>

#include <cstdlib>
#include <fstream>
#include <vector>

// Load a SPIR-V binary file from disk. Returns an empty vector on failure.
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

// Generate a simple checkerboard texture (magenta / white) for the demo.
// Returns a flat RGBA8 buffer; dimensions are tileSize * tiles in each axis.
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

int main(int /*argc*/, char* /*argv*/[])
{
    Arcbit::Log::Init();
    LOG_INFO(Engine, "Arcbit starting");

    // --- Window -------------------------------------------------------------
    Arcbit::Window window({ .Title = "Arcbit", .Width = 1280, .Height = 720 });

    // --- Renderer -----------------------------------------------------------
    Arcbit::DeviceDesc deviceDesc{};
    deviceDesc.NativeWindowHandle = window.GetNativeHandle();
    deviceDesc.AppName            = "Arcbit";
    deviceDesc.AppVersion         = 1;
#ifdef ARCBIT_DEBUG
    deviceDesc.EnableValidation   = true;
#endif

    Arcbit::RenderDevice* device = Arcbit_CreateDevice(deviceDesc);
    ARCBIT_ASSERT(device != nullptr, "Failed to create render device");
    LOG_INFO(Render, "Render device ready");

    // --- Swapchain ----------------------------------------------------------
    Arcbit::SwapchainDesc swapDesc{};
    swapDesc.NativeWindowHandle = window.GetNativeHandle();
    swapDesc.Width              = window.GetWidth();
    swapDesc.Height             = window.GetHeight();
    swapDesc.VSync              = true;

    Arcbit::SwapchainHandle swapchain = device->CreateSwapchain(swapDesc);
    ARCBIT_ASSERT(swapchain.IsValid(), "Failed to create swapchain");

    const Arcbit::Format swapFormat = device->GetSwapchainColorFormat(swapchain);

    // --- Triangle pipeline (solid colour, kept for reference) ---------------
    auto vertSpv = LoadSpv("shaders/triangle.vert.spv");
    auto fragSpv = LoadSpv("shaders/triangle.frag.spv");

    Arcbit::ShaderHandle triVert = device->CreateShader({ Arcbit::ShaderStage::Vertex,   vertSpv.data(), static_cast<Arcbit::u32>(vertSpv.size()) });
    Arcbit::ShaderHandle triFrag = device->CreateShader({ Arcbit::ShaderStage::Fragment, fragSpv.data(), static_cast<Arcbit::u32>(fragSpv.size()) });

    Arcbit::PipelineDesc triDesc{};
    triDesc.VertexShader   = triVert;
    triDesc.FragmentShader = triFrag;
    triDesc.CullMode       = Arcbit::CullMode::None;
    triDesc.ColorFormat    = swapFormat;
    triDesc.DepthFormat    = Arcbit::Format::Undefined;
    triDesc.DebugName      = "TrianglePipeline";

    Arcbit::PipelineHandle triPipeline = device->CreatePipeline(triDesc);
    ARCBIT_ASSERT(triPipeline.IsValid(), "Failed to create triangle pipeline");
    device->DestroyShader(triVert);
    device->DestroyShader(triFrag);

    // --- Quad pipeline (textured) -------------------------------------------
    auto quadVertSpv = LoadSpv("shaders/quad.vert.spv");
    auto quadFragSpv = LoadSpv("shaders/quad.frag.spv");

    Arcbit::ShaderHandle quadVert = device->CreateShader({ Arcbit::ShaderStage::Vertex,   quadVertSpv.data(), static_cast<Arcbit::u32>(quadVertSpv.size()) });
    Arcbit::ShaderHandle quadFrag = device->CreateShader({ Arcbit::ShaderStage::Fragment, quadFragSpv.data(), static_cast<Arcbit::u32>(quadFragSpv.size()) });

    Arcbit::PipelineDesc quadDesc{};
    quadDesc.VertexShader   = quadVert;
    quadDesc.FragmentShader = quadFrag;
    quadDesc.CullMode       = Arcbit::CullMode::None;
    quadDesc.ColorFormat    = swapFormat;
    quadDesc.DepthFormat    = Arcbit::Format::Undefined;
    quadDesc.UseTextures    = true;
    quadDesc.DebugName      = "QuadPipeline";

    Arcbit::PipelineHandle quadPipeline = device->CreatePipeline(quadDesc);
    ARCBIT_ASSERT(quadPipeline.IsValid(), "Failed to create quad pipeline");
    device->DestroyShader(quadVert);
    device->DestroyShader(quadFrag);

    // --- Checkerboard texture -----------------------------------------------
    constexpr Arcbit::u32 CheckerTileSize = 32;
    constexpr Arcbit::u32 CheckerTiles    = 4;
    constexpr Arcbit::u32 CheckerDim      = CheckerTileSize * CheckerTiles;

    auto checkerPixels = MakeCheckerboard(CheckerTileSize, CheckerTiles);

    Arcbit::TextureDesc texDesc{};
    texDesc.Width     = CheckerDim;
    texDesc.Height    = CheckerDim;
    texDesc.Format    = Arcbit::Format::RGBA8_UNorm;
    texDesc.Usage     = Arcbit::TextureUsage::Sampled | Arcbit::TextureUsage::Transfer;
    texDesc.DebugName = "Checkerboard";

    Arcbit::TextureHandle checkerTex = device->CreateTexture(texDesc);
    ARCBIT_ASSERT(checkerTex.IsValid(), "Failed to create checkerboard texture");
    device->UploadTexture(checkerTex, checkerPixels.data(),
        static_cast<Arcbit::u64>(checkerPixels.size()));

    // --- Sampler ------------------------------------------------------------
    Arcbit::SamplerDesc sampDesc{};
    sampDesc.MinFilter = Arcbit::Filter::Nearest;
    sampDesc.MagFilter = Arcbit::Filter::Nearest;
    sampDesc.AddressU  = Arcbit::AddressMode::Repeat;
    sampDesc.AddressV  = Arcbit::AddressMode::Repeat;
    sampDesc.DebugName = "NearestRepeat";

    Arcbit::SamplerHandle sampler = device->CreateSampler(sampDesc);
    ARCBIT_ASSERT(sampler.IsValid(), "Failed to create sampler");

    LOG_INFO(Render, "Texture and sampler ready");

    // --- Render thread ------------------------------------------------------
    Arcbit::RenderThread renderThread;
    renderThread.Start(device);

    // --- Game loop ----------------------------------------------------------
    LOG_INFO(Engine, "Entering game loop");

    while (window.PollEvents())
    {
        Arcbit::FramePacket packet{};
        packet.Swapchain   = swapchain;
        packet.Width       = window.GetWidth();
        packet.Height      = window.GetHeight();
        packet.ClearColor  = { 0.05f, 0.05f, 0.15f, 1.00f };
        packet.NeedsResize = window.WasResizedThisFrame();

        Arcbit::DrawCall quadDraw{};
        quadDraw.Pipeline    = quadPipeline;
        quadDraw.Texture     = checkerTex;
        quadDraw.Sampler     = sampler;
        quadDraw.VertexCount = 6;
        packet.DrawCalls.push_back(quadDraw);

        renderThread.SubmitFrame(std::move(packet));
    }

    // --- Shutdown -----------------------------------------------------------
    // Stop render thread before touching any GPU resources.
    renderThread.Stop();
    device->WaitIdle();
    device->DestroySampler(sampler);
    device->DestroyTexture(checkerTex);
    device->DestroyPipeline(quadPipeline);
    device->DestroyPipeline(triPipeline);
    device->DestroySwapchain(swapchain);
    Arcbit_DestroyDevice(device);
    // Window destructor calls SDL_DestroyWindow + SDL_Quit.

    LOG_INFO(Engine, "Arcbit shutdown complete");
    Arcbit::Log::Shutdown();
    return EXIT_SUCCESS;
}
