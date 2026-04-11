#include <arcbit/arcbit.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Profiler.h>
#include <arcbit/render/RenderDevice.h>

#include <SDL3/SDL.h>

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

    // --- Event loop ---------------------------------------------------------
    LOG_INFO(Engine, "Entering event loop — Escape or close window to exit");

    bool running = true;
    while (running)
    {
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
                default:
                    break;
            }
        }
    }

    // --- Shutdown -----------------------------------------------------------
    device->WaitIdle();
    Arcbit_DestroyDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    LOG_INFO(Engine, "Arcbit shutdown complete");
    Arcbit::Log::Shutdown();
    return EXIT_SUCCESS;
}
