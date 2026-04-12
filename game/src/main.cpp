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

    // --- Shaders & Pipeline -------------------------------------------------
    // Shaders are compiled to .spv at build time by glslc (see game/CMakeLists.txt).
    // The working directory must be the binary output directory (build/debug/bin).
    auto vertSpv = LoadSpv("shaders/triangle.vert.spv");
    auto fragSpv = LoadSpv("shaders/triangle.frag.spv");

    Arcbit::ShaderDesc vertDesc{};
    vertDesc.Stage    = Arcbit::ShaderStage::Vertex;
    vertDesc.Code     = vertSpv.data();
    vertDesc.CodeSize = static_cast<Arcbit::u32>(vertSpv.size());
    Arcbit::ShaderHandle vertShader = device->CreateShader(vertDesc);

    Arcbit::ShaderDesc fragDesc{};
    fragDesc.Stage    = Arcbit::ShaderStage::Fragment;
    fragDesc.Code     = fragSpv.data();
    fragDesc.CodeSize = static_cast<Arcbit::u32>(fragSpv.size());
    Arcbit::ShaderHandle fragShader = device->CreateShader(fragDesc);

    Arcbit::PipelineDesc pipelineDesc{};
    pipelineDesc.VertexShader   = vertShader;
    pipelineDesc.FragmentShader = fragShader;
    pipelineDesc.CullMode       = Arcbit::CullMode::None;       // no backface culling for 2D
    pipelineDesc.ColorFormat    = device->GetSwapchainColorFormat(swapchain);
    pipelineDesc.DepthFormat    = Arcbit::Format::Undefined;    // no depth buffer yet
    pipelineDesc.DebugName      = "TrianglePipeline";

    Arcbit::PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ARCBIT_ASSERT(pipeline.IsValid(), "Failed to create pipeline");

    // Shader modules can be destroyed immediately after pipeline creation —
    // Vulkan copies the bytecode internally during vkCreateGraphicsPipelines.
    device->DestroyShader(vertShader);
    device->DestroyShader(fragShader);

    LOG_INFO(Render, "Pipeline ready");

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

        // Clear to a deep blue, then draw the orange triangle on top.
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
        device->BindPipeline(cmd, pipeline);
        device->SetViewport(cmd, 0.0f, 0.0f,
            static_cast<Arcbit::f32>(windowWidth),
            static_cast<Arcbit::f32>(windowHeight));
        device->SetScissor(cmd, 0, 0, windowWidth, windowHeight);
        device->Draw(cmd, 3); // 3 vertices — positions are hardcoded in the vertex shader
        device->EndRendering(cmd);

        device->EndCommandList(cmd);
        device->Submit({ cmd });
        device->Present(swapchain);
    }

    // --- Shutdown -----------------------------------------------------------
    device->WaitIdle();
    device->DestroyPipeline(pipeline);
    device->DestroySwapchain(swapchain);
    Arcbit_DestroyDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    LOG_INFO(Engine, "Arcbit shutdown complete");
    Arcbit::Log::Shutdown();
    return EXIT_SUCCESS;
}
