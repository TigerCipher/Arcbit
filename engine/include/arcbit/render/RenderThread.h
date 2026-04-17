#pragma once

#include <arcbit/render/RenderDevice.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/assets/AssetTypes.h>
#include <arcbit/core/Math.h>

#include <array>
#include <atomic>
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
    // Center of the light in world-space pixels, matching Sprite::Position.
    Vec2 Position;

    // Radius in world-space pixels. The light falls off to zero at this distance.
    f32 Radius = 200.0f;

    // Multiplier applied to the light color. >1 is valid for HDR-style intensity.
    f32 Intensity = 1.0f;

    // RGBA light color. Alpha is not used for transparency — it is reserved
    // for future use (e.g. a falloff exponent).
    Color LightColor = Color::White();
};

// ---------------------------------------------------------------------------
// Sprite
//
// A world-space sprite submitted by the game thread each frame.
// The render thread sorts sprites by layer then groups them by texture to
// minimize descriptor switches, then issues one instanced draw call per group.
//
// Coordinate system
// -----------------
// Position and Size are in world-space pixels. The camera (CameraPosition on
// FramePacket) defines which world point maps to screen center. At zoom 1:
//   NDC = (worldPos - cameraPos) / (viewportSize * 0.5)
// Y+ is downward (screen-style), consistent with most 2D games.
// ---------------------------------------------------------------------------
struct Sprite
{
    TextureHandle Texture;                            // albedo / sprite texture (set 0, required)
    TextureHandle NormalTexture;                      // normal map (set 1); leave invalid for flat-normal fallback
    SamplerHandle Sampler;                            // filtering / addressing for albedo and normal

    Vec2   Position = { 0.0f, 0.0f };                // world-space center in pixels
    Vec2   Size     = { 64.0f, 64.0f };              // full width × height in pixels
    UVRect UV       = { 0.0f, 0.0f, 1.0f, 1.0f };   // normalized sub-region; defaults to whole texture
    Color  Tint     = Color::White();                 // per-sprite RGBA tint multiplied against the albedo

    // Draw order. Sprites are rendered lower-layer-first (painter's algorithm).
    // Sprites on the same layer with the same texture are batched into one draw.
    i32 Layer = 0;
};

// ---------------------------------------------------------------------------
// DrawCall  (low-level escape hatch — prefer Sprite for game code)
//
// Directly specifies NDC-space position, scale, and UV, and requires the
// caller to own the pipeline. Rendered after all Sprites, in submission order.
// Useful for special effects or post-processing quads that don't fit the
// sprite batcher model.
// ---------------------------------------------------------------------------
struct DrawCall
{
    PipelineHandle Pipeline;        // pipeline to bind (must be a forward-compatible pipeline)
    TextureHandle  Texture;         // albedo texture (set 0)
    TextureHandle  NormalTexture;   // normal map (set 1); leave invalid for flat-normal fallback
    SamplerHandle  Sampler;
    u32            VertexCount = 0;

    // NDC-space position and half-size.  (0,0) = screen center; (1,1) = bottom-right.
    Vec2   Position = { 0.0f, 0.0f };
    Vec2   Scale    = { 1.0f, 1.0f };
    UVRect UV       = { 0.0f, 0.0f, 1.0f, 1.0f };
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

    // High-level sprite list. Sorted by layer then grouped by texture; the
    // render thread issues one instanced draw per texture group.
    std::vector<Sprite> Sprites;

    // World-space position that maps to the center of the screen.
    // Defaults to (0, 0) — a sprite at world (0, 0) renders at screen center.
    Vec2 CameraPosition = { 0.0f, 0.0f };

    // Zoom multiplier. >1 zooms in (world pixels appear larger), <1 zooms out.
    // Applied by dividing the effective viewport size before the NDC transform.
    f32 CameraZoom = 1.0f;

    // Low-level draw calls rendered after all sprites, in submission order.
    // Requires the caller to own the pipeline and specify NDC coordinates.
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
// RenderStats
//
// Snapshot of per-frame rendering statistics written by the render thread
// after each frame and readable from any thread via RenderThread::GetStats().
// Values reflect the most recently completed frame.
// ---------------------------------------------------------------------------
struct RenderStats
{
    u32 SpritesSubmitted = 0;  // total sprites in packet.Sprites
    u32 SpriteBatches    = 0;  // instanced GPU draw calls emitted by the batcher
    u32 LegacyDrawCalls  = 0;  // GPU draw calls from packet.DrawCalls
    u32 LightsActive     = 0;  // point lights uploaded to the SSBO
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
    // swapchainFormat is needed to create the sprite pipeline with the correct
    // color attachment format — pass RenderDevice::GetSwapchainColorFormat().
    void Start(RenderDevice* device, Format swapchainFormat);

    // Signal the thread to exit and block until it does.
    // After this returns, no Vulkan calls are in flight and the device can
    // safely be destroyed.
    void Stop();

    // Hand a completed FramePacket to the render thread for rendering.
    // Blocks briefly if the render thread is still finishing the previous frame.
    void SubmitFrame(FramePacket packet);

    // Returns a snapshot of the statistics from the most recently completed frame.
    // Safe to call from any thread — values are written with relaxed atomics.
    [[nodiscard]] RenderStats GetStats() const;


private:
    // Thread entry point — loops calling RenderFrame until Stop() is called.
    void Run();

    // Execute one frame: acquire image, record commands, submit, present.
    void RenderFrame(const FramePacket& packet);

    // ----- Start() helpers (called once on the render thread at startup) -----

    void CreateDefaultNormalResources();
    void CreateSpritePipeline(Format swapchainFormat);
    void CreateInstanceBuffers();
    void CreateLightSSBOs();

    // ----- RenderFrame() helpers (called once per frame) ---------------------

    // Handles NeedsResize and AcquireNextImage with one automatic retry.
    // Returns an invalid handle if the frame should be skipped.
    [[nodiscard]] TextureHandle AcquireBackbuffer(const FramePacket& packet) const;

    // Grows the light SSBO if needed, uploads this frame's lights.
    // Returns the light count for use in push constants.
    [[nodiscard]] u32 UploadLights(const FramePacket& packet, u32 frameSlot);

    // Returns pointers into packet.Sprites sorted by (layer, texture, sampler).
    [[nodiscard]] static std::vector<const Sprite*> SortSprites(const std::vector<Sprite>& sprites);

    // Grows the instance buffer if needed, uploads sorted sprite data.
    void UploadInstances(const std::vector<const Sprite*>& sorted, u32 frameSlot);

    // Records BeginRendering with a single cleared color attachment.
    void BeginRenderPass(CommandListHandle cmd, TextureHandle backbuffer,
                         const FramePacket& packet) const;

    // Binds the sprite pipeline, uploads push constants, and issues one
    // instanced draw call per texture group. Returns the batch count.
    [[nodiscard]] u32 DrawSpriteBatches(CommandListHandle cmd,
                                        const std::vector<const Sprite*>& sorted,
                                        u32 frameSlot, const FramePacket& packet,
                                        u32 lightCount) const;

    // Issues legacy DrawCall entries in submission order, after all sprites.
    void DrawLegacyCalls(CommandListHandle cmd, const FramePacket& packet,
                         u32 frameSlot, u32 lightCount) const;

private:
    
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

    // ----- Per-frame render statistics (atomic — written by render thread) --

    std::atomic<u32> m_StatSpritesSubmitted{0};
    std::atomic<u32> m_StatSpriteBatches{0};
    std::atomic<u32> m_StatDrawCalls{0};
    std::atomic<u32> m_StatLightsActive{0};

    // ----- Shared GPU resources — owned by the render thread ---------------

    static constexpr u32 FrameSlots = RenderDevice::MaxFramesInFlight;

    // Default 1×1 RGBA texture encoding a flat normal (0.5, 0.5, 1.0, 1.0).
    // Bound to set 1 when a Sprite or DrawCall provides no NormalTexture.
    TextureHandle m_DefaultNormalTex;
    SamplerHandle m_DefaultNormalSampler;

    // ----- Sprite batcher resources ----------------------------------------

    // Pipeline created from sprite.vert + sprite.frag in Start().
    // Per-instance vertex layout: 3 × vec4 (posSize, uv, tint) = 48 bytes/sprite.
    PipelineHandle m_SpritePipeline;

    // Per-frame instance buffers. The CPU writes sprite instances into
    // m_InstanceBuffers[currentFrame]; the other slot is still being read.
    std::array<BufferHandle, FrameSlots> m_InstanceBuffers = {};

    // Allocated capacity in each instance buffer (number of sprites).
    u32 m_InstanceBufferCapacity = 0;

    // ----- Forward+ light SSBO resources -----------------------------------

    // One SSBO per frame-in-flight slot — same cycling rationale as instance buffers.
    std::array<BufferHandle, FrameSlots> m_LightSSBO = {};
    u32 m_LightSSBOCapacity = 0;
};

} // namespace Arcbit
