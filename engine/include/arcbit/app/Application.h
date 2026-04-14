#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/assets/TextureManager.h>

#include <array>
#include <memory>

namespace Arcbit {

class Window; // full type only needed in Application.cpp

// ---------------------------------------------------------------------------
// ApplicationConfig
//
// Passed to the Application constructor to set startup defaults.
// These values act as fallbacks: if a settings.json file exists on disk,
// the persisted graphics settings take precedence over Width/Height.
// ---------------------------------------------------------------------------
struct ApplicationConfig
{
    const char* Title        = "Arcbit";
    u32         Width        = 1280;
    u32         Height       = 720;
    bool        Resizable    = true;
    const char* SettingsPath = "settings.json";
};

// ---------------------------------------------------------------------------
// Application
//
// The root class of every Arcbit game. Owns all engine systems and runs the
// main loop. Subclass it and override the virtual hooks to add game logic.
//
// Typical usage
// -------------
//   class MyGame : public Arcbit::Application {
//   public:
//       MyGame() : Application({ .Title = "My Game" }) {}
//   protected:
//       void OnStart()               override { /* load assets, bind keys */ }
//       void OnUpdate(f32 dt)        override { /* game logic */ }
//       void OnRender(FramePacket&)  override { /* push draw calls */ }
//       void OnShutdown()            override { /* free GPU resources */ }
//   };
//
//   int main(int, char*[]) { MyGame game; game.Run(); }
//
// System startup / shutdown order
// --------------------------------
//   Startup  (constructor):  Log → Settings → Window → Device → Swapchain → RenderThread
//   Runtime  (Run):          OnStart → LoadInputBindings → [loop] → OnShutdown
//   Shutdown (Run tail):     RenderThread → Device → Settings → Log
//
// Thread safety
// -------------
//   All hooks (OnStart/OnUpdate/OnRender/OnShutdown) and system accessors are
//   called from the main thread. The render thread is managed internally.
// ---------------------------------------------------------------------------
class Application
{
public:
    explicit Application(const ApplicationConfig& config = {});

    // Destructor is defined in Application.cpp so the incomplete Window type
    // is available (unique_ptr requires the complete type at destruction).
    virtual ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    // Start the main loop. Blocks until the window is closed.
    // Call this exactly once after constructing the Application subclass.
    void Run();

    // --- System accessors (available from OnStart onwards) ---

    // The render device — use in OnStart / OnShutdown to create / destroy
    // pipelines, textures, buffers, and samplers.
    [[nodiscard]] RenderDevice& GetDevice();

    // The input manager — use in OnStart to register actions and add bindings,
    // then query it in OnUpdate.
    [[nodiscard]] InputManager& GetInput() { return m_Input; }

    // The window — use in OnStart / OnUpdate for size queries or title changes.
    [[nodiscard]] Window& GetWindow();

    // The texture manager — load textures from disk in OnStart; the manager
    // caches by path so repeated loads are free. Application clears it
    // automatically during shutdown, but you can also Unload() explicitly.
    [[nodiscard]] TextureManager& GetTextures() { return *m_Textures; }

    // Color format of the swapchain — pass to PipelineDesc::ColorFormat so
    // pipelines match the actual surface format chosen by the driver.
    [[nodiscard]] Format GetSwapchainFormat() const;

protected:
    // -----------------------------------------------------------------------
    // Game hooks — override these in your Application subclass.
    // -----------------------------------------------------------------------

    // Called once after all systems are initialized, before the loop begins.
    // Register input actions, create pipelines, load assets here.
    virtual void OnStart() {}

    // Called at a fixed rate (fixedTimestep seconds per call) for game logic
    // that must be frame-rate-independent (physics, AI, gameplay state).
    virtual void OnUpdate(f32 dt) {}

    // Called every frame (variable rate) to build the render packet.
    // The packet is pre-filled with Swapchain, Width, Height, NeedsResize,
    // and ClearColor. Push your DrawCalls into packet.DrawCalls.
    virtual void OnRender(FramePacket& packet) {}

    // Called once after the loop exits, before systems are shut down.
    // Destroy GPU resources (pipelines, textures, buffers) here.
    virtual void OnShutdown() {}

    // -----------------------------------------------------------------------
    // Tunable knobs — set in the subclass constructor or OnStart.
    // -----------------------------------------------------------------------

    // Background clear color used to pre-fill each FramePacket.
    std::array<f32, 4> _clearColor = { 0.05f, 0.05f, 0.15f, 1.0f };

    // Target rate for OnUpdate calls. Defaults to 60 Hz.
    // Change before Run() is called; modifying mid-loop is not recommended.
    f32 _fixedTimestep = 1.0f / 60.0f;

private:
    ApplicationConfig m_Config;

    // Member declaration order determines both initialization and destruction
    // order (destruction is reverse of initialization).
    //
    // Required destruction order:
    //   m_Input        — closes SDL gamepad handles (SDL must still be alive)
    //   m_Window       — calls SDL_Quit (must outlive InputManager)
    //   m_RenderThread — stopped explicitly in Run(); destructor is a no-op
    //   m_Swapchain    — handle only; destroyed explicitly in Run()
    //   m_Textures     — unique_ptr; reset() called explicitly before DestroyDevice
    //   m_Device       — raw pointer; destroyed explicitly in Run()
    //
    // So declaration order is the reverse of that (destructor fires bottom-up):
    RenderDevice*                    m_Device = nullptr;
    std::unique_ptr<TextureManager>  m_Textures;          // created after device; reset before DestroyDevice
    SwapchainHandle                  m_Swapchain;
    RenderThread                     m_RenderThread;
    std::unique_ptr<Window>          m_Window;            // SDL_Quit in destructor
    InputManager                     m_Input;             // SDL gamepad cleanup in destructor

    // Accumulates real time to rate-limit hot-reload checks to once per second.
    f64 m_HotReloadAccumulator = 0.0;
};

} // namespace Arcbit
