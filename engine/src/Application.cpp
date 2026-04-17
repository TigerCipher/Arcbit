#include <arcbit/app/Application.h>
#include <arcbit/app/Window.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <chrono>
#include <thread>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Constructor — initializes all engine systems in dependency order.
// ---------------------------------------------------------------------------

Application::Application(const ApplicationConfig& config) : m_Config(config)
{
    Log::Init();
    LOG_INFO(Engine, "Arcbit starting");

    // Seed Settings with config defaults so that GraphicsSettings has sensible
    // values even when no settings file exists yet. ReadFile() will then
    // overwrite these with any persisted values.
    Settings::Graphics.ResolutionWidth  = config.Width;
    Settings::Graphics.ResolutionHeight = config.Height;
    Settings::Init(config.SettingsPath);

    m_Window = std::make_unique<Window>(Window::Desc{
        .Title     = config.Title,
        .Width     = Settings::Graphics.ResolutionWidth,
        .Height    = Settings::Graphics.ResolutionHeight,
        .Resizable = config.Resizable,
    });

    DeviceDesc deviceDesc{};
    deviceDesc.NativeWindowHandle = m_Window->GetNativeHandle();
    deviceDesc.AppName            = config.Title;
    deviceDesc.AppVersion         = 1;
#ifdef ARCBIT_DEBUG
    deviceDesc.EnableValidation = true;
#endif
    m_Device = Arcbit_CreateDevice(deviceDesc);
    ARCBIT_ASSERT(m_Device != nullptr, "Failed to create render device");
    LOG_INFO(Render, "Render device ready");

    SwapchainDesc swapDesc{};
    swapDesc.NativeWindowHandle = m_Window->GetNativeHandle();
    swapDesc.Width              = m_Window->GetWidth();
    swapDesc.Height             = m_Window->GetHeight();
    swapDesc.VSync              = Settings::Graphics.VSync;
    m_Swapchain                 = m_Device->CreateSwapchain(swapDesc);
    ARCBIT_ASSERT(m_Swapchain.IsValid(), "Failed to create swapchain");

    m_RenderThread.Start(m_Device, m_Device->GetSwapchainColorFormat(m_Swapchain));

    m_Textures = std::make_unique<TextureManager>(*m_Device);
}

// ---------------------------------------------------------------------------
// Destructor — defined here so the compiler sees Window's complete type.
// ---------------------------------------------------------------------------

Application::~Application() = default;

// ---------------------------------------------------------------------------
// System accessors
// ---------------------------------------------------------------------------

RenderDevice& Application::GetDevice()
{
    ARCBIT_ASSERT(m_Device != nullptr, "GetDevice() called after shutdown");
    return *m_Device;
}

Window& Application::GetWindow()
{ return *m_Window; }

Format Application::GetSwapchainFormat() const
{ return m_Device->GetSwapchainColorFormat(m_Swapchain); }

// ---------------------------------------------------------------------------
// Run — the main loop.
// ---------------------------------------------------------------------------

void Application::Run()
{
    // Give the game a chance to register actions, create GPU resources, etc.
    // Input bindings are loaded from settings AFTER OnStart so that default
    // bindings set up in OnStart are overwritten by any user-saved bindings.
    OnStart();
    Settings::LoadInputBindings(m_Input);

    // Route Window input events to InputManager so JustPressed/JustReleased are
    // driven by the SDL event queue rather than polling-based state comparison.
    // This prevents edge events from being missed when the fixed-timestep
    // accumulator fires zero update ticks in a given display frame.
    m_Window->SetKeyEventCallback([this](int sc, bool down) {
        m_Input.InjectKeyEvent(sc, down);
    });
    m_Window->SetMouseButtonCallback([this](int btn, bool down) {
        m_Input.InjectMouseButton(btn, down);
    });
    m_Window->SetGamepadButtonCallback([this](u32 which, int btn, bool down) {
        m_Input.InjectGamepadButton(which, btn, down);
    });

    // High-resolution wall-clock timer for accurate delta time.
    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::duration<f64>;

    auto prevTime    = Clock::now();
    f64  accumulator = 0.0;

    // FPS counter state — reported once per second via LOG_TRACE.
    auto fpsWindowStart = Clock::now();
    u32  fpsFrameCount  = 0;

    LOG_INFO(Engine, "Entering game loop");

    while (m_Window->PollEvents())
    {
        // Record the frame start time before any work so the FPS limiter can
        // measure the total cost of this frame (including SubmitFrame's back-
        // pressure wait) and sleep only the time that remains.
        const auto frameStart = Clock::now();

        // Input must be updated after PollEvents so SDL's internal state
        // reflects all events from this tick.
        m_Input.Update();

        // --- Fixed-timestep update -------------------------------------------
        // Accumulate real elapsed time and drain it in fixed-size chunks so
        // game logic runs at a predictable rate regardless of frame rate.
        // The 250 ms cap prevents a "spiral of death" if the app stalls
        // (breakpoint hit, disk stall, etc.) and the accumulator explodes.
        const f64 dt = std::min(Duration(frameStart - prevTime).count(), 0.25);
        prevTime     = frameStart;

        accumulator += dt;
        while (accumulator >= static_cast<f64>(_fixedTimestep))
        {
            // ProcessEdges() consumes accumulated key/mouse/gamepad button events
            // and sets JustPressed/JustReleased for this tick. Events are held in
            // the pending sets until ProcessEdges() runs, so they survive frames
            // where the accumulator doesn't reach the threshold.
            m_Input.ProcessEdges();
            OnUpdate(_fixedTimestep);
            accumulator -= static_cast<f64>(_fixedTimestep);
        }

        // --- Variable-rate render --------------------------------------------
        FramePacket packet{};
        packet.Swapchain   = m_Swapchain;
        packet.Width       = m_Window->GetWidth();
        packet.Height      = m_Window->GetHeight();
        packet.ClearColor  = _clearColor;
        packet.NeedsResize = m_Window->WasResizedThisFrame();

        OnRender(packet);

        m_RenderThread.SubmitFrame(std::move(packet));

        // --- FPS counter + render stats --------------------------------------
        // Count completed frames; report once per second alongside the most
        // recent frame's render statistics from the render thread.
        ++fpsFrameCount;
        if (const f64 fpsElapsed = Duration(Clock::now() - fpsWindowStart).count(); fpsElapsed >= 1.0)
        {
            const RenderStats stats = m_RenderThread.GetStats();
            LOG_TRACE(Engine, "FPS: {:.1f} | Sprites: {} ({} batches) | DrawCalls: {} | Lights: {}",
                      static_cast<f64>(fpsFrameCount) / fpsElapsed, stats.SpritesSubmitted, stats.SpriteBatches,
                      stats.LegacyDrawCalls, stats.LightsActive);
            fpsFrameCount  = 0;
            fpsWindowStart = Clock::now();
        }

        // --- Asset hot-reload ------------------------------------------------
        // Checked once per second rather than every frame to avoid the cost
        // of stat()-ing every loaded texture path each tick.
        m_HotReloadAccumulator += dt;
        if (m_HotReloadAccumulator >= 1.0)
        {
            m_Textures->CheckReloads();
            m_HotReloadAccumulator -= 1.0;
        }

        // --- FPS limiter -----------------------------------------------------
        // Only active when VSync is off and a limit is configured.
        // sleep_for is imprecise (~1-2 ms granularity on Windows), which is
        // acceptable here — VSync is the preferred path for exact pacing.
        if (!Settings::Graphics.VSync && Settings::Graphics.FpsLimit > 0)
        {
            const f64 targetFrameTime = 1.0 / static_cast<f64>(Settings::Graphics.FpsLimit);
            const f64 frameTime       = Duration(Clock::now() - frameStart).count();
            if (const f64 remaining = targetFrameTime - frameTime; remaining > 0.001)
                // skip sleep for sub-millisecond remainders
                std::this_thread::sleep_for(std::chrono::duration<f64>(remaining));
        }
    }

    // --- Shutdown sequence ---------------------------------------------------
    // GPU work must be fully drained before any Vulkan object is destroyed.
    // Stop the render thread first (joins the thread, so the last frame is
    // guaranteed to have been submitted), then WaitIdle to flush the GPU queue.
    // Only then is it safe for OnShutdown to destroy pipelines, textures, etc.
    m_RenderThread.Stop();
    m_Device->WaitIdle();

    OnShutdown();

    // Release all cached textures before the device is destroyed.
    m_Textures->Clear();
    m_Textures.reset();

    m_Device->DestroySwapchain(m_Swapchain);
    Arcbit_DestroyDevice(m_Device);
    m_Device = nullptr;

    // Capture any runtime rebinds before writing the settings file.
    Settings::SaveInputBindings(m_Input);
    Settings::Shutdown();

    LOG_INFO(Engine, "Arcbit shutdown complete");
    Log::Shutdown();
}
} // namespace Arcbit
