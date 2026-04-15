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
// PointLight
//
// A dynamic point light submitted by the game thread each frame.
// The render thread uploads the full Lights list to a GPU SSBO each frame
// and passes lightCount via push constants. No fixed array limit.
// ---------------------------------------------------------------------------
struct PointLight
{
    // Center of the light in NDC space (-1 to 1 on each axis), matching the
    // coordinate system used by DrawCall::Position.
    Vec2 Position;

    // Radius of the light in NDC units. 1.0 reaches the screen edges from center.
    f32 Radius = 0.5f;

    // Multiplier applied to the light color. >1 is valid for HDR-style intensity.
    f32 Intensity = 1.0f;

    // RGBA light color. Alpha is not used for transparency — it is reserved
    // for future use (e.g. a falloff exponent).
    Color LightColor = Color::White();
};

// ---------------------------------------------------------------------------
// DrawCall
//
// A single draw call baked into a FramePacket by the game thread.
// The render thread issues one quad draw per entry, binding the albedo and
// optional normal textures and reading lights from the per-frame SSBO.
// ---------------------------------------------------------------------------
struct DrawCall
{
    PipelineHandle Pipeline;        // forward+ sprite pipeline to bind
    TextureHandle  Texture;         // albedo / sprite texture (set 0)
    TextureHandle  NormalTexture;   // normal map (set 1); leave invalid for the flat-normal fallback
    SamplerHandle  Sampler;         // used for both albedo and normal texture samples
    u32            VertexCount = 0; // vertices to emit (6 for a quad)

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

    // Dynamic point light list. Uploaded to a per-frame SSBO each tick.
    // The forward+ fragment shader loops over all entries.
    std::vector<PointLight> Lights;

    // Additive ambient light applied to every pixel regardless of light proximity.
    Color AmbientColor = Color{ 0.1f, 0.1f, 0.1f, 1.0f };

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

    // ----- Forward+ resources — owned by the render thread -----------------

    // One SSBO per frame-in-flight slot. The render thread uploads lights to
    // m_LightSSBO[currentFrame] each tick; the other slot is still being read
    // by the GPU, so writing to it would race.
    static constexpr u32 GBufferSlots = RenderDevice::MaxFramesInFlight;
    std::array<BufferHandle, GBufferSlots> m_LightSSBO = {};

    // Capacity (in light entries) currently allocated for each SSBO slot.
    // When the Lights list grows beyond this, the SSBO is recreated.
    u32 m_LightSSBOCapacity = 0;

    // Default 1×1 RGBA texture encoding a flat normal (0.5, 0.5, 1.0, 1.0).
    // Bound to set 1 when a DrawCall does not provide a NormalTexture.
    TextureHandle m_DefaultNormalTex;

    // Nearest-neighbor sampler shared for the default normal texture.
    SamplerHandle m_DefaultNormalSampler;
};

} // namespace Arcbit
