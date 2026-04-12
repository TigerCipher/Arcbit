#include <arcbit/arcbit.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Profiler.h>
#include <arcbit/render/RenderDevice.h>

#include <SDL3/SDL.h>

#include <array>
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

// Generate a simple 4x4 checkerboard texture (magenta / white) for testing
// Returns a flat RGBA8 buffer; width and height are always 4.
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

    // --- Platform -----------------------------------------------------------
    ARCBIT_VERIFY(SDL_Init(SDL_INIT_VIDEO), "SDL_Init failed");

    SDL_Window* window = SDL_CreateWindow(
        "Arcbit",
        1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );
    ARCBIT_ASSERT(window != nullptr, "SDL_CreateWindow failed");
    LOG_INFO(Platform, "Window created (1280x720)");

    // --- Renderer -----------------------------------------------------------
    Arcbit::DeviceDesc deviceDesc{};
    deviceDesc.NativeWindowHandle = window;
    deviceDesc.AppName            = "Arcbit";
    deviceDesc.AppVersion         = 1;
#ifdef ARCBIT_DEBUG
    deviceDesc.EnableValidation   = true;
#endif

    Arcbit::RenderDevice* device = Arcbit_CreateDevice(deviceDesc);
    ARCBIT_ASSERT(device != nullptr, "Failed to create render device");
    LOG_INFO(Render, "Render device ready");

    // --- Swapchain ----------------------------------------------------------
    Arcbit::u32 windowWidth  = 1280;
    Arcbit::u32 windowHeight = 720;

    Arcbit::SwapchainDesc swapDesc{};
    swapDesc.NativeWindowHandle = window;
    swapDesc.Width              = windowWidth;
    swapDesc.Height             = windowHeight;
    swapDesc.VSync              = true;

    Arcbit::SwapchainHandle swapchain = device->CreateSwapchain(swapDesc);
    ARCBIT_ASSERT(swapchain.IsValid(), "Failed to create swapchain");

    const Arcbit::Format swapFormat = device->GetSwapchainColorFormat(swapchain);

    // --- Triangle pipeline (solid colour, no texture) -----------------------
    {
        // Kept alive only long enough to show it still compiles. Not drawn in
        // the main loop — the quad demo replaces it for this phase.
    }
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
    quadDesc.UseTextures    = true;  // include texture descriptor set layout
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
    texDesc.Width    = CheckerDim;
    texDesc.Height   = CheckerDim;
    texDesc.Format   = Arcbit::Format::RGBA8_UNorm;
    texDesc.Usage    = Arcbit::TextureUsage::Sampled | Arcbit::TextureUsage::Transfer;
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

    // --- Event loop ---------------------------------------------------------
    LOG_INFO(Engine, "Entering event loop — Escape or close window to exit");

    bool running = true;
    while (running)
    {
        // Poll window events.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE)
                        running = false;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                    windowWidth  = static_cast<Arcbit::u32>(event.window.data1);
                    windowHeight = static_cast<Arcbit::u32>(event.window.data2);
                    device->ResizeSwapchain(swapchain, windowWidth, windowHeight);
                    break;
                default:
                    break;
            }
        }

        // --- Frame ----------------------------------------------------------
        Arcbit::TextureHandle backbuffer = device->AcquireNextImage(swapchain);
        if (!backbuffer.IsValid()) continue;

        Arcbit::CommandListHandle cmd = device->BeginCommandList();

        Arcbit::Attachment colorAttach{};
        colorAttach.Texture       = backbuffer;
        colorAttach.Load          = Arcbit::LoadOp::Clear;
        colorAttach.Store         = Arcbit::StoreOp::Store;
        colorAttach.ClearColor[0] = 0.05f;
        colorAttach.ClearColor[1] = 0.05f;
        colorAttach.ClearColor[2] = 0.15f;
        colorAttach.ClearColor[3] = 1.00f;

        std::array<Arcbit::Attachment, 1> colorAttachments = { colorAttach };
        Arcbit::RenderingDesc renderDesc{};
        renderDesc.ColorAttachments = colorAttachments;

        device->BeginRendering(cmd, renderDesc);

        // Draw the textured quad covering the full screen.
        device->BindPipeline(cmd, quadPipeline);
        device->SetViewport(cmd, 0.0f, 0.0f,
            static_cast<Arcbit::f32>(windowWidth),
            static_cast<Arcbit::f32>(windowHeight));
        device->SetScissor(cmd, 0, 0, windowWidth, windowHeight);
        device->BindTexture(cmd, checkerTex, sampler);
        device->Draw(cmd, 6); // 6 vertices — two triangles forming the quad

        device->EndRendering(cmd);

        device->EndCommandList(cmd);
        device->Submit({ cmd });
        device->Present(swapchain);
    }

    // --- Shutdown -----------------------------------------------------------
    device->WaitIdle();
    device->DestroySampler(sampler);
    device->DestroyTexture(checkerTex);
    device->DestroyPipeline(quadPipeline);
    device->DestroyPipeline(triPipeline);
    device->DestroySwapchain(swapchain);
    Arcbit_DestroyDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    LOG_INFO(Engine, "Arcbit shutdown complete");
    Arcbit::Log::Shutdown();
    return EXIT_SUCCESS;
}
