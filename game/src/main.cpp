#include <arcbit/arcbit.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Profiler.h>
#include <arcbit/render/RenderDevice.h>

#include <SDL3/SDL.h>

#include <array>
#include <cstdlib>

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
    Arcbit::SwapchainDesc swapDesc{};
    swapDesc.NativeWindowHandle = window;
    swapDesc.Width              = 1280;
    swapDesc.Height             = 720;
    swapDesc.VSync              = true;

    Arcbit::SwapchainHandle swapchain = device->CreateSwapchain(swapDesc);
    ARCBIT_ASSERT(swapchain.IsValid(), "Failed to create swapchain");

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
                    device->ResizeSwapchain(swapchain,
                        static_cast<Arcbit::u32>(event.window.data1),
                        static_cast<Arcbit::u32>(event.window.data2));
                    break;
                default:
                    break;
            }
        }

        // --- Frame ----------------------------------------------------------

        // Acquire the next swapchain image. Returns invalid if the swapchain
        // is out of date; SDL_EVENT_WINDOW_RESIZED will follow and trigger a resize.
        Arcbit::TextureHandle backbuffer = device->AcquireNextImage(swapchain);
        if (!backbuffer.IsValid()) continue;

        Arcbit::CommandListHandle cmd = device->BeginCommandList();

        // Clear the backbuffer to a deep-blue colour.
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
        // Draw calls go here once the pipeline is implemented (Phase 6).
        device->EndRendering(cmd);

        device->EndCommandList(cmd);
        device->Submit({ cmd });
        device->Present(swapchain);
    }

    // --- Shutdown -----------------------------------------------------------
    device->WaitIdle();
    device->DestroySwapchain(swapchain);
    Arcbit_DestroyDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    LOG_INFO(Engine, "Arcbit shutdown complete");
    Arcbit::Log::Shutdown();
    return EXIT_SUCCESS;
}
