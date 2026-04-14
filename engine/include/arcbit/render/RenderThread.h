#pragma once

#include <arcbit/render/RenderDevice.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/assets/AssetTypes.h>

#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace Arcbit {

// ---------------------------------------------------------------------------
// DrawCall
//
// A single draw call baked into a FramePacket by the game thread.
// The render thread translates it into BindPipeline / BindTexture / Draw
// calls against the RenderDevice — the game thread never touches those APIs
// directly after startup.
// ---------------------------------------------------------------------------
struct DrawCall
{
    PipelineHandle Pipeline;        // graphics pipeline to bind
    TextureHandle  Texture;         // leave invalid (default) for untextured draws
    SamplerHandle  Sampler;         // only consulted when Texture.IsValid()
    u32            VertexCount = 0; // vertices to emit (e.g. 6 for a quad)

    // Position of the quad's center in NDC space (-1 to 1 on each axis).
    // (0, 0) is the screen center; (-1,-1) is top-left; (1,1) is bottom-right.
    Vec2 Position = { 0.0f, 0.0f };

    // Half-size of the quad in NDC space. (1, 1) fills the entire screen.
    // For a 256×256 sprite on a 1280×720 window, use roughly (0.2, 0.35).
    Vec2 Scale = { 1.0f, 1.0f };

    // Sub-region of the texture to sample, in normalized (0-1) UV space.
    // Defaults to the full texture. Use SpriteSheet::GetTile() / GetSprite()
    // to get the UV rect for a specific frame and assign it here.
    UVRect UV = { 0.0f, 0.0f, 1.0f, 1.0f };
};

// ---------------------------------------------------------------------------
// FramePacket
//
// All state the render thread needs to produce one frame.
// Built each tick by the game thread and handed to RenderThread::SubmitFrame.
//
// Why a struct instead of direct device calls?
// -----------------------------------------------
// The game thread and render thread run concurrently. If the game thread
// called RenderDevice directly it would race with the render thread's own
// Vulkan calls. FramePacket is the handshake point: the game thread writes
// it, signals the render thread, then immediately starts building the *next*
// packet — both threads are busy at the same time with no shared mutable state.
// ---------------------------------------------------------------------------
struct FramePacket
{
    SwapchainHandle Swapchain;         // which swapchain to present to
    u32             Width     = 0;     // current backbuffer dimensions in pixels
    u32             Height    = 0;
    Color           ClearColor = Color::Black(); // RGBA clear color for the color attachment

    // Ordered list of draw calls. The render thread executes them in order
    // inside a single BeginRendering / EndRendering scope.
    std::vector<DrawCall> DrawCalls;

    // If true, the render thread calls ResizeSwapchain(Width, Height) before
    // acquiring the next image. Set this when the window has been resized.
    // The flag is consumed once and does not persist across frames.
    bool NeedsResize = false;
};

// ---------------------------------------------------------------------------
// RenderThread
//
// Owns a dedicated OS thread that drives all per-frame GPU work.
//
// Threading model
// ---------------
//   Game thread  — builds FramePackets, calls SubmitFrame each tick.
//   Render thread — wakes on each SubmitFrame, runs Acquire→Record→Submit→
//                   Present, then sleeps waiting for the next packet.
//
// Back-pressure
// -------------
//   SubmitFrame blocks if the render thread hasn't finished the previous frame
//   yet. This ensures the game thread stays at most one frame ahead of the
//   GPU, which caps input latency and prevents memory usage from growing
//   without bound if the game logic ever runs faster than the renderer.
//
//   Diagram (time flows right):
//
//     Game  [build N]──submit──[build N+1]──wait──────submit──[build N+2]
//     Render           [render N]──────────────────[render N+1]
//                       ^                            ^
//                       game blocks here if render   game can go here as
//                       isn't done with N yet        soon as N is done
//
// Lifetime
// --------
//   1. Create resources (device, swapchain, pipelines, textures …)
//   2. renderThread.Start(device)
//   3. Main loop: call SubmitFrame each tick
//   4. renderThread.Stop()   ← blocks until all GPU work finishes
//   5. device->WaitIdle()    ← belt-and-suspenders
//   6. Destroy resources
// ---------------------------------------------------------------------------
class RenderThread
{
public:
    // Launch the render thread. device must outlive the RenderThread.
    void Start(RenderDevice* device);

    // Signal the thread to exit and block until it does.
    // After this returns, no Vulkan calls are in flight and the device can
    // safely be destroyed.
    void Stop();

    // Hand a completed FramePacket to the render thread for rendering.
    // Blocks briefly if the render thread is still finishing the previous frame.
    void SubmitFrame(FramePacket packet);

private:
    // Thread entry point — loops calling RenderFrame until Stop() is called.
    void Run();

    // Execute one frame: resize if requested, acquire image, record commands,
    // submit, present. Called only from the render thread.
    void RenderFrame(const FramePacket& packet);

    RenderDevice* m_Device = nullptr;
    std::thread   m_Thread;

    // ----- Shared state — always accessed under m_Mutex --------------------

    std::mutex m_Mutex;

    // Render thread sleeps here waiting for the game thread to submit a frame.
    std::condition_variable m_FrameReady;

    // Game thread sleeps here when it is already one frame ahead of the render
    // thread (m_SlotFree == false). This is the back-pressure mechanism.
    std::condition_variable m_FrameDone;

    FramePacket m_Packet;           // pending frame written by SubmitFrame
    bool        m_HasFrame = false; // true while m_Packet holds an unrendered frame
    bool        m_SlotFree = true;  // true once the render thread has consumed the packet
    bool        m_Running  = false; // set to false by Stop() to signal the thread to exit
};

} // namespace Arcbit
