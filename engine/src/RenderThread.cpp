#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <vector>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// GPU-side structs
// ---------------------------------------------------------------------------

// Point light layout — must match std430 in sprite.frag / forward.frag:
//   struct PointLight { vec2 position; float radius; float intensity; vec4 color; };
// 32 bytes, naturally aligned.
struct GpuPointLight
{
    f32 PosX, PosY;
    f32 Radius;
    f32 Intensity;
    f32 ColorR, ColorG, ColorB, ColorA;
};

// Per-sprite instance data uploaded to the instance vertex buffer.
// Packed as 3 × vec4 so all attributes use Format::RGBA32_Float.
// Must match the layout(location = 0/1/2) inputs in sprite.vert.
struct SpriteInstance
{
    f32 PosX, PosY, HalfW, HalfH;   // world-space center + half-extents (location 0)
    f32 U0, V0, U1, V1;             // normalized UV rect                (location 1)
    f32 TintR, TintG, TintB, TintA; // RGBA tint                         (location 2)
}; // 48 bytes

// Push constants for the sprite pipeline.
// Same block declared in both sprite.vert and sprite.frag; each stage
// only reads the fields relevant to it.
//   Vertex:   camPos, viewportSize
//   Fragment: ambient, lightCount
struct SpritePushConstants
{
    f32 CamPosX, CamPosY;
    f32 ViewportW, ViewportH;
    f32 RotCos, RotSin;         // precomputed cos/sin of camera rotation
    f32 AmbientR, AmbientG, AmbientB, AmbientA;
    u32 LightCount;
}; // 44 bytes

// Push constants for the legacy DrawCall (forward) pipeline.
// Must match ForwardPushConstants layout in forward.vert / forward.frag.
struct ForwardPushConstants
{
    f32 PositionX, PositionY;
    f32 ScaleX, ScaleY;
    f32 U0, V0, U1, V1;
    f32 AmbientR, AmbientG, AmbientB, AmbientA;
    u32 LightCount;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

std::vector<u8> LoadShaderFile(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ARCBIT_ASSERT(file.is_open(), "RenderThread: failed to open shader file — check working directory");
    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<u8> buf(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Start() helpers
// ---------------------------------------------------------------------------

void RenderThread::CreateDefaultNormalResources()
{
    // (128, 128, 255, 255) → normalized (0.502, 0.502, 1.0) in the shader,
    // which decodes to a normal pointing straight out of the screen (+Z tangent).
    TextureDesc normalDesc{};
    normalDesc.Width     = 1;
    normalDesc.Height    = 1;
    normalDesc.Format    = Format::RGBA8_UNorm;
    normalDesc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
    normalDesc.DebugName = "default_normal";
    _defaultNormalTex   = _device->CreateTexture(normalDesc);

    const u8 flatNormal[4] = { 128, 128, 255, 255 };
    _device->UploadTexture(_defaultNormalTex, flatNormal, sizeof(flatNormal));

    SamplerDesc sampDesc{};
    sampDesc.MinFilter     = Filter::Nearest;
    sampDesc.MagFilter     = Filter::Nearest;
    sampDesc.AddressU      = AddressMode::ClampToEdge;
    sampDesc.AddressV      = AddressMode::ClampToEdge;
    sampDesc.DebugName     = "default_normal_sampler";
    _defaultNormalSampler = _device->CreateSampler(sampDesc);
}

void RenderThread::CreateSpritePipeline(const Format swapchainFormat)
{
    auto vertSpv = LoadShaderFile("shaders/sprite.vert.spv");
    auto fragSpv = LoadShaderFile("shaders/sprite.frag.spv");

    const ShaderHandle vert =
        _device->CreateShader({ ShaderStage::Vertex, vertSpv.data(), static_cast<u32>(vertSpv.size()), "main", "sprite.vert" });
    const ShaderHandle frag = _device->CreateShader(
        { ShaderStage::Fragment, fragSpv.data(), static_cast<u32>(fragSpv.size()), "main", "sprite.frag" });

    // Three vec4 instance attributes — each 16 bytes, packed in one binding.
    // PerInstance = true → VK_VERTEX_INPUT_RATE_INSTANCE (one entry per sprite).
    const VertexAttribute attrs[] = {
        { 0, 0, Format::RGBA32_Float,  0 }, // a_PosSize  (posX, posY, halfW, halfH)
        { 1, 0, Format::RGBA32_Float, 16 }, // a_UV       (u0, v0, u1, v1)
        { 2, 0, Format::RGBA32_Float, 32 }, // a_Tint     (r, g, b, a)
    };
    const VertexBinding bindings[] = {
        { 0, static_cast<u32>(sizeof(SpriteInstance)), true }, // per-instance
    };

    PipelineDesc desc{};
    desc.VertexShader     = vert;
    desc.FragmentShader   = frag;
    desc.Attributes       = attrs;
    desc.Bindings         = bindings;
    desc.CullMode         = CullMode::None;
    desc.ColorFormat      = swapchainFormat;
    desc.DepthFormat      = Format::Undefined;
    desc.UseTextures      = true; // set 0 = albedo
    desc.UseNormalTexture = true; // set 1 = normal map
    desc.UseStorageBuffer = true; // set 2 = light SSBO
    // Standard premultiplied-alpha blend for sprite transparency.
    desc.Blend.Enable   = true;
    desc.Blend.SrcColor = BlendFactor::SrcAlpha;
    desc.Blend.DstColor = BlendFactor::OneMinusSrcAlpha;
    desc.Blend.ColorOp  = BlendOp::Add;
    desc.Blend.SrcAlpha = BlendFactor::One;
    desc.Blend.DstAlpha = BlendFactor::Zero;
    desc.Blend.AlphaOp  = BlendOp::Add;
    desc.DebugName      = "SpritePipeline";

    _spritePipeline = _device->CreatePipeline(desc);
    ARCBIT_ASSERT(_spritePipeline.IsValid(), "RenderThread: failed to create sprite pipeline");

    _device->DestroyShader(vert);
    _device->DestroyShader(frag);
}

void RenderThread::CreateInstanceBuffers()
{
    static constexpr u32 InitialCapacity = 256;
    _instanceBufferCapacity             = InitialCapacity;

    BufferDesc desc{};
    desc.Size        = InitialCapacity * sizeof(SpriteInstance);
    desc.Usage       = BufferUsage::Vertex;
    desc.HostVisible = true;
    desc.DebugName   = "sprite_instances";

    for (auto& buf : _instanceBuffers)
        buf = _device->CreateBuffer(desc);
}

void RenderThread::CreateLightSSBOs()
{
    static constexpr u32 InitialCapacity = 64;
    _lightSSBOCapacity                  = InitialCapacity;

    BufferDesc desc{};
    desc.Size        = InitialCapacity * sizeof(GpuPointLight);
    desc.Usage       = BufferUsage::Storage;
    desc.HostVisible = true;
    desc.DebugName   = "light_ssbo";

    for (auto& ssbo : _lightSSBO)
        ssbo = _device->CreateBuffer(desc);
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

void RenderThread::Start(RenderDevice* device, const Format swapchainFormat)
{
    _device = device;

    CreateDefaultNormalResources();
    CreateSpritePipeline(swapchainFormat);
    CreateInstanceBuffers();
    CreateLightSSBOs();

    _running = true;
    _thread  = std::thread(&RenderThread::Run, this);
    LOG_INFO(Render, "Render thread started");
}

void RenderThread::Stop()
{
    {
        std::lock_guard lock(_mutex);
        _running = false;
    }
    _frameReady.notify_one();

    if (_thread.joinable())
        _thread.join();

    // GPU must be idle before any resources are freed. WaitIdle() here (rather
    // than in Application::Run) keeps the GPU-sync responsibility with the
    // render thread that submitted the work.
    _device->WaitIdle();

    for (auto& ssbo : _lightSSBO)
        if (ssbo.IsValid())
            _device->DestroyBuffer(ssbo);

    for (auto& buf : _instanceBuffers)
        if (buf.IsValid())
            _device->DestroyBuffer(buf);

    if (_spritePipeline.IsValid())
        _device->DestroyPipeline(_spritePipeline);
    if (_defaultNormalSampler.IsValid())
        _device->DestroySampler(_defaultNormalSampler);
    if (_defaultNormalTex.IsValid())
        _device->DestroyTexture(_defaultNormalTex);

    LOG_INFO(Render, "Render thread stopped");
}

// ---------------------------------------------------------------------------
// Frame submission (game thread side)
// ---------------------------------------------------------------------------

RenderStats RenderThread::GetStats() const
{
    return {
        _statSpritesSubmitted.load(std::memory_order_relaxed),
        _statSpriteBatches.load(std::memory_order_relaxed),
        _statDrawCalls.load(std::memory_order_relaxed),
        _statLightsActive.load(std::memory_order_relaxed),
    };
}

void RenderThread::SubmitFrame(FramePacket packet)
{
    std::unique_lock lock(_mutex);
    _frameDone.wait(lock, [this] { return _slotFree; });

    _packet   = std::move(packet);
    _hasFrame = true;
    _slotFree = false;

    lock.unlock();
    _frameReady.notify_one();
}

// ---------------------------------------------------------------------------
// Render thread loop
// ---------------------------------------------------------------------------

void RenderThread::Run()
{
    while (true)
    {
        FramePacket packet;
        {
            std::unique_lock lock(_mutex);
            _frameReady.wait(lock, [this] { return _hasFrame || !_running; });
            if (!_running && !_hasFrame)
                break;
            packet     = std::move(_packet);
            _hasFrame = false;
        }

        RenderFrame(packet);

        {
            std::lock_guard lock(_mutex);
            _slotFree = true;
        }
        _frameDone.notify_one();
    }
}

// ---------------------------------------------------------------------------
// RenderFrame() helpers
// ---------------------------------------------------------------------------

TextureHandle RenderThread::AcquireBackBuffer(const FramePacket& packet) const
{
    if (packet.NeedsResize)
        _device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);

    TextureHandle backBuffer = _device->AcquireNextImage(packet.Swapchain);
    if (backBuffer.IsValid())
        return backBuffer;

    // Swapchain is out of date (e.g. during fullscreen exit) but the packet
    // dimensions are stale — SDL fires the resize event a few ms later on the
    // game thread, so calling ResizeSwapchain here would build a swapchain at
    // the wrong size, trigger vkDeviceWaitIdle, and deadlock SubmitFrame.
    // Skip the frame and let the NeedsResize packet fix it on the next frame.
    LOG_WARN(Render, "Swapchain out of date at acquire - skipping frame, waiting for resize");
    return {};
}

u32 RenderThread::UploadLights(const FramePacket& packet, const u32 frameSlot)
{
    const u32 lightCount = static_cast<u32>(packet.Lights.size());

    if (lightCount > _lightSSBOCapacity)
    {
        _device->WaitIdle();
        u32 newCapacity = _lightSSBOCapacity;
        while (newCapacity < lightCount)
            newCapacity *= 2;
        _lightSSBOCapacity = newCapacity;

        BufferDesc desc{};
        desc.Size        = newCapacity * sizeof(GpuPointLight);
        desc.Usage       = BufferUsage::Storage;
        desc.HostVisible = true;
        desc.DebugName   = "light_ssbo";

        for (auto& ssbo : _lightSSBO)
        {
            _device->DestroyBuffer(ssbo);
            ssbo = _device->CreateBuffer(desc);
        }
        LOG_INFO(Render, "Light SSBO resized to {} entries", newCapacity);
    }

    if (lightCount > 0)
    {
        std::vector<GpuPointLight> gpuLights;
        gpuLights.reserve(lightCount);
        for (const auto& [Position, Radius, Intensity, LightColor] : packet.Lights)
        {
            GpuPointLight& g = gpuLights.emplace_back();
            g.PosX           = Position.X;
            g.PosY           = Position.Y;
            g.Radius         = Radius;
            g.Intensity      = Intensity;
            g.ColorR         = LightColor.R;
            g.ColorG         = LightColor.G;
            g.ColorB         = LightColor.B;
            g.ColorA         = LightColor.A;
        }
        _device->UpdateBuffer(_lightSSBO[frameSlot], gpuLights.data(), lightCount * sizeof(GpuPointLight));
    }

    return lightCount;
}

std::vector<const Sprite*> RenderThread::SortSprites(const std::vector<Sprite>& sprites)
{
    std::vector<const Sprite*> sorted;
    sorted.reserve(sprites.size());
    for (const Sprite& s : sprites)
        sorted.push_back(&s);

    std::ranges::stable_sort(sorted, [](const Sprite* a, const Sprite* b) {
        if (a->Layer != b->Layer)
            return a->Layer < b->Layer;
        if (a->Texture.Index() != b->Texture.Index())
            return a->Texture.Index() < b->Texture.Index();
        return a->Sampler.Index() < b->Sampler.Index();
    });

    return sorted;
}

void RenderThread::UploadInstances(const std::vector<const Sprite*>& sorted, const u32 frameSlot)
{
    const u32 spriteCount = static_cast<u32>(sorted.size());
    if (spriteCount == 0)
        return;

    if (spriteCount > _instanceBufferCapacity)
    {
        _device->WaitIdle();
        u32 newCapacity = _instanceBufferCapacity;
        while (newCapacity < spriteCount)
            newCapacity *= 2;
        _instanceBufferCapacity = newCapacity;

        BufferDesc desc{};
        desc.Size        = newCapacity * sizeof(SpriteInstance);
        desc.Usage       = BufferUsage::Vertex;
        desc.HostVisible = true;
        desc.DebugName   = "sprite_instances";

        for (auto& buf : _instanceBuffers)
        {
            _device->DestroyBuffer(buf);
            buf = _device->CreateBuffer(desc);
        }
        LOG_INFO(Render, "Instance buffer resized to {} sprites", newCapacity);
    }

    std::vector<SpriteInstance> instances;
    instances.reserve(spriteCount);
    for (const Sprite* s : sorted)
    {
        SpriteInstance& inst = instances.emplace_back();
        inst.PosX            = s->Position.X;
        inst.PosY            = s->Position.Y;
        inst.HalfW           = s->Size.X * 0.5f;
        inst.HalfH           = s->Size.Y * 0.5f;
        inst.U0              = s->UV.U0;
        inst.V0              = s->UV.V0;
        inst.U1              = s->UV.U1;
        inst.V1              = s->UV.V1;
        inst.TintR           = s->Tint.R;
        inst.TintG           = s->Tint.G;
        inst.TintB           = s->Tint.B;
        inst.TintA           = s->Tint.A;
    }
    _device->UpdateBuffer(_instanceBuffers[frameSlot], instances.data(), spriteCount * sizeof(SpriteInstance));
}

RenderThread::LetterboxRect RenderThread::ComputeLetterbox(const FramePacket& packet)
{
    const f32 actualW = static_cast<f32>(packet.Width);
    const f32 actualH = static_cast<f32>(packet.Height);

    if (packet.ReferenceSize.X <= 0.0f || packet.ReferenceSize.Y <= 0.0f || actualH == 0.0f)
        return { 0.0f, 0.0f, actualW, actualH };

    const f32 designAspect = packet.ReferenceSize.X / packet.ReferenceSize.Y;
    const f32 actualAspect = actualW / actualH;

    if (actualAspect > designAspect)
    {
        // Window is wider — pillarbox (bars on left and right).
        const f32 w = actualH * designAspect;
        return { std::round((actualW - w) * 0.5f), 0.0f, std::round(w), actualH };
    }
    else
    {
        // Window is taller — letterbox (bars on top and bottom).
        const f32 h = actualW / designAspect;
        return { 0.0f, std::round((actualH - h) * 0.5f), actualW, std::round(h) };
    }
}

void RenderThread::BeginRenderPass(const CommandListHandle cmd, const TextureHandle backBuffer,
                                   const FramePacket& packet, const LetterboxRect& lb) const
{
    // Clear the full framebuffer to LetterboxColor (fills bars and interior).
    Attachment colorAttach{};
    colorAttach.Texture    = backBuffer;
    colorAttach.Load       = LoadOp::Clear;
    colorAttach.Store      = StoreOp::Store;
    colorAttach.ClearColor = packet.LetterboxColor;

    std::array<Attachment, 1> attachments = { colorAttach };
    RenderingDesc             renderDesc{};
    renderDesc.ColorAttachments = attachments;
    _device->BeginRendering(cmd, renderDesc);

    // If the game interior has a different clear color, fill the letterbox rect.
    const bool hasBars = lb.X > 0.5f || lb.Y > 0.5f;
    if (hasBars || packet.ClearColor != packet.LetterboxColor)
    {
        _device->ClearColorRect(cmd, packet.ClearColor,
                                static_cast<i32>(lb.X), static_cast<i32>(lb.Y),
                                static_cast<u32>(lb.W), static_cast<u32>(lb.H));
    }
}

u32 RenderThread::DrawSpriteBatches(const CommandListHandle cmd, const std::vector<const Sprite*>& sorted,
                                    const u32 frameSlot, const FramePacket& packet,
                                    const u32 lightCount, const LetterboxRect& lb) const
{
    const u32 spriteCount = static_cast<u32>(sorted.size());
    if (spriteCount == 0)
        return 0;

    _device->BindPipeline(cmd, _spritePipeline);
    _device->SetViewport(cmd, lb.X, lb.Y, lb.W, lb.H);
    _device->SetScissor(cmd, static_cast<i32>(lb.X), static_cast<i32>(lb.Y),
                        static_cast<u32>(lb.W), static_cast<u32>(lb.H));
    _device->BindStorageBuffer(cmd, _lightSSBO[frameSlot], 2, 0);

    // Use the reference resolution for the NDC transform when set, so the same world
    // area is always visible regardless of window size. Fall back to the letterbox
    // rect dimensions (which equal the full window when no reference is set).
    const f32 refW = (packet.ReferenceSize.X > 0.0f) ? packet.ReferenceSize.X : lb.W;
    const f32 refH = (packet.ReferenceSize.Y > 0.0f) ? packet.ReferenceSize.Y : lb.H;

    SpritePushConstants pc{};
    pc.CamPosX    = packet.CameraPosition.X;
    pc.CamPosY    = packet.CameraPosition.Y;
    pc.ViewportW  = refW / packet.CameraZoom;
    pc.ViewportH  = refH / packet.CameraZoom;
    pc.RotCos     = std::cos(packet.CameraRotation);
    pc.RotSin     = std::sin(packet.CameraRotation);
    pc.AmbientR   = packet.AmbientColor.R;
    pc.AmbientG   = packet.AmbientColor.G;
    pc.AmbientB   = packet.AmbientColor.B;
    pc.AmbientA   = packet.AmbientColor.A;
    pc.LightCount = lightCount;
    _device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment, &pc, sizeof(pc));

    u32 batchCount = 0;
    u32 groupStart = 0;
    while (groupStart < spriteCount)
    {
        const Sprite* first    = sorted[groupStart];
        u32           groupEnd = groupStart + 1;

        while (groupEnd < spriteCount)
        {
            const Sprite* next = sorted[groupEnd];
            if (next->Texture.Index() != first->Texture.Index() || next->Sampler.Index() != first->Sampler.Index() ||
                next->NormalTexture.Index() != first->NormalTexture.Index())
                break;
            ++groupEnd;
        }

        // Albedo — set 0.
        _device->BindTexture(cmd, first->Texture, first->Sampler, 0);

        // Normal map — set 1. Fall back to the flat-normal default.
        const bool    hasNormal     = first->NormalTexture.IsValid();
        const TextureHandle normalTex     = hasNormal ? first->NormalTexture : _defaultNormalTex;
        const SamplerHandle normalSampler = hasNormal ? first->Sampler : _defaultNormalSampler;
        _device->BindTexture(cmd, normalTex, normalSampler, 1);

        // Offset into the instance buffer for this group's first sprite.
        _device->BindVertexBuffer(cmd, _instanceBuffers[frameSlot], 0, static_cast<u64>(groupStart) * sizeof(SpriteInstance));

        _device->Draw(cmd, 6, 0, groupEnd - groupStart, 0);
        ++batchCount;
        groupStart = groupEnd;
    }

    return batchCount;
}

void RenderThread::DrawLegacyCalls(const CommandListHandle cmd, const FramePacket& packet,
                                   const u32 frameSlot, const u32 lightCount,
                                   const LetterboxRect& lb) const
{
    for (const DrawCall& dc : packet.DrawCalls)
    {
        _device->BindPipeline(cmd, dc.Pipeline);
        _device->SetViewport(cmd, lb.X, lb.Y, lb.W, lb.H);
        _device->SetScissor(cmd, static_cast<i32>(lb.X), static_cast<i32>(lb.Y),
                            static_cast<u32>(lb.W), static_cast<u32>(lb.H));
        _device->BindStorageBuffer(cmd, _lightSSBO[frameSlot], 2, 0);

        if (dc.Texture.IsValid())
            _device->BindTexture(cmd, dc.Texture, dc.Sampler, 0);

        const bool    hasNormal     = dc.NormalTexture.IsValid();
        const TextureHandle normalTex     = hasNormal ? dc.NormalTexture : _defaultNormalTex;
        const SamplerHandle normalSampler = hasNormal ? dc.Sampler : _defaultNormalSampler;
        _device->BindTexture(cmd, normalTex, normalSampler, 1);

        ForwardPushConstants fpc{};
        fpc.PositionX  = dc.Position.X;
        fpc.PositionY  = dc.Position.Y;
        fpc.ScaleX     = dc.Scale.X;
        fpc.ScaleY     = dc.Scale.Y;
        fpc.U0         = dc.UV.U0;
        fpc.V0         = dc.UV.V0;
        fpc.U1         = dc.UV.U1;
        fpc.V1         = dc.UV.V1;
        fpc.AmbientR   = packet.AmbientColor.R;
        fpc.AmbientG   = packet.AmbientColor.G;
        fpc.AmbientB   = packet.AmbientColor.B;
        fpc.AmbientA   = packet.AmbientColor.A;
        fpc.LightCount = lightCount;
        _device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment, &fpc, sizeof(fpc));

        _device->Draw(cmd, dc.VertexCount);
    }
}

// ---------------------------------------------------------------------------
// RenderFrame — orchestrates one frame on the render thread
// ---------------------------------------------------------------------------

void RenderThread::RenderFrame(const FramePacket& packet)
{
    const TextureHandle backbuffer = AcquireBackBuffer(packet);
    if (!backbuffer.IsValid())
        return;

    const u32 frameSlot  = _device->GetCurrentFrameIndex(packet.Swapchain);
    const u32 lightCount = UploadLights(packet, frameSlot);

    // Query the actual swapchain extent after any resize that AcquireBackbuffer
    // may have triggered. packet.Width/Height can be stale by one frame (the SDL
    // resize event races with the render thread), which would cause ClearColorRect
    // to request a rect larger than the active render pass.
    u32 actualW = packet.Width, actualH = packet.Height;
    _device->GetSwapchainExtent(packet.Swapchain, actualW, actualH);

    // Build a corrected packet view with the true dimensions for all rect math.
    FramePacket corrected  = packet; // shallow copy — Sprites/Lights are ptrs in sorted/uploaded form
    corrected.Width        = actualW;
    corrected.Height       = actualH;

    const LetterboxRect lb = ComputeLetterbox(corrected);

    const auto sorted = SortSprites(packet.Sprites);
    UploadInstances(sorted, frameSlot);

    CommandListHandle cmd = _device->BeginCommandList();
    BeginRenderPass(cmd, backbuffer, corrected, lb);

    const u32 batchCount = DrawSpriteBatches(cmd, sorted, frameSlot, corrected, lightCount, lb);
    DrawLegacyCalls(cmd, corrected, frameSlot, lightCount, lb);

    _device->EndRendering(cmd);

    _statSpritesSubmitted.store(static_cast<u32>(sorted.size()), std::memory_order_relaxed);
    _statSpriteBatches.store(batchCount, std::memory_order_relaxed);
    _statDrawCalls.store(static_cast<u32>(packet.DrawCalls.size()) + batchCount, std::memory_order_relaxed);
    _statLightsActive.store(lightCount, std::memory_order_relaxed);

    _device->EndCommandList(cmd);
    _device->Submit({ cmd });
    _device->Present(packet.Swapchain);
}

} // namespace Arcbit
