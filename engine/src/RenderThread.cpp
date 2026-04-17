#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <algorithm>
#include <array>
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
    f32 AmbientR, AmbientG, AmbientB, AmbientA;
    u32 LightCount;
}; // 36 bytes

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
    m_DefaultNormalTex   = m_Device->CreateTexture(normalDesc);

    const u8 flatNormal[4] = { 128, 128, 255, 255 };
    m_Device->UploadTexture(m_DefaultNormalTex, flatNormal, sizeof(flatNormal));

    SamplerDesc sampDesc{};
    sampDesc.MinFilter     = Filter::Nearest;
    sampDesc.MagFilter     = Filter::Nearest;
    sampDesc.AddressU      = AddressMode::ClampToEdge;
    sampDesc.AddressV      = AddressMode::ClampToEdge;
    sampDesc.DebugName     = "default_normal_sampler";
    m_DefaultNormalSampler = m_Device->CreateSampler(sampDesc);
}

void RenderThread::CreateSpritePipeline(const Format swapchainFormat)
{
    auto vertSpv = LoadShaderFile("shaders/sprite.vert.spv");
    auto fragSpv = LoadShaderFile("shaders/sprite.frag.spv");

    const ShaderHandle vert =
        m_Device->CreateShader({ ShaderStage::Vertex, vertSpv.data(), static_cast<u32>(vertSpv.size()), "main", "sprite.vert" });
    const ShaderHandle frag = m_Device->CreateShader(
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

    m_SpritePipeline = m_Device->CreatePipeline(desc);
    ARCBIT_ASSERT(m_SpritePipeline.IsValid(), "RenderThread: failed to create sprite pipeline");

    m_Device->DestroyShader(vert);
    m_Device->DestroyShader(frag);
}

void RenderThread::CreateInstanceBuffers()
{
    static constexpr u32 InitialCapacity = 256;
    m_InstanceBufferCapacity             = InitialCapacity;

    BufferDesc desc{};
    desc.Size        = InitialCapacity * sizeof(SpriteInstance);
    desc.Usage       = BufferUsage::Vertex;
    desc.HostVisible = true;
    desc.DebugName   = "sprite_instances";

    for (auto& buf : m_InstanceBuffers)
        buf = m_Device->CreateBuffer(desc);
}

void RenderThread::CreateLightSSBOs()
{
    static constexpr u32 InitialCapacity = 64;
    m_LightSSBOCapacity                  = InitialCapacity;

    BufferDesc desc{};
    desc.Size        = InitialCapacity * sizeof(GpuPointLight);
    desc.Usage       = BufferUsage::Storage;
    desc.HostVisible = true;
    desc.DebugName   = "light_ssbo";

    for (auto& ssbo : m_LightSSBO)
        ssbo = m_Device->CreateBuffer(desc);
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

void RenderThread::Start(RenderDevice* device, const Format swapchainFormat)
{
    m_Device = device;

    CreateDefaultNormalResources();
    CreateSpritePipeline(swapchainFormat);
    CreateInstanceBuffers();
    CreateLightSSBOs();

    m_Running = true;
    m_Thread  = std::thread(&RenderThread::Run, this);
    LOG_INFO(Render, "Render thread started");
}

void RenderThread::Stop()
{
    {
        std::lock_guard lock(m_Mutex);
        m_Running = false;
    }
    m_FrameReady.notify_one();

    if (m_Thread.joinable())
        m_Thread.join();

    // GPU must be idle before any resources are freed. WaitIdle() here (rather
    // than in Application::Run) keeps the GPU-sync responsibility with the
    // render thread that submitted the work.
    m_Device->WaitIdle();

    for (auto& ssbo : m_LightSSBO)
        if (ssbo.IsValid())
            m_Device->DestroyBuffer(ssbo);

    for (auto& buf : m_InstanceBuffers)
        if (buf.IsValid())
            m_Device->DestroyBuffer(buf);

    if (m_SpritePipeline.IsValid())
        m_Device->DestroyPipeline(m_SpritePipeline);
    if (m_DefaultNormalSampler.IsValid())
        m_Device->DestroySampler(m_DefaultNormalSampler);
    if (m_DefaultNormalTex.IsValid())
        m_Device->DestroyTexture(m_DefaultNormalTex);

    LOG_INFO(Render, "Render thread stopped");
}

// ---------------------------------------------------------------------------
// Frame submission (game thread side)
// ---------------------------------------------------------------------------

RenderStats RenderThread::GetStats() const
{
    return {
        m_StatSpritesSubmitted.load(std::memory_order_relaxed),
        m_StatSpriteBatches.load(std::memory_order_relaxed),
        m_StatDrawCalls.load(std::memory_order_relaxed),
        m_StatLightsActive.load(std::memory_order_relaxed),
    };
}

void RenderThread::SubmitFrame(FramePacket packet)
{
    std::unique_lock lock(m_Mutex);
    m_FrameDone.wait(lock, [this] { return m_SlotFree; });

    m_Packet   = std::move(packet);
    m_HasFrame = true;
    m_SlotFree = false;

    lock.unlock();
    m_FrameReady.notify_one();
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
            std::unique_lock lock(m_Mutex);
            m_FrameReady.wait(lock, [this] { return m_HasFrame || !m_Running; });
            if (!m_Running && !m_HasFrame)
                break;
            packet     = std::move(m_Packet);
            m_HasFrame = false;
        }

        RenderFrame(packet);

        {
            std::lock_guard lock(m_Mutex);
            m_SlotFree = true;
        }
        m_FrameDone.notify_one();
    }
}

// ---------------------------------------------------------------------------
// RenderFrame() helpers
// ---------------------------------------------------------------------------

TextureHandle RenderThread::AcquireBackbuffer(const FramePacket& packet) const
{
    if (packet.NeedsResize)
        m_Device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);

    TextureHandle backbuffer = m_Device->AcquireNextImage(packet.Swapchain);
    if (backbuffer.IsValid())
        return backbuffer;

    // Swapchain was out of date — recreate and retry once.
    LOG_WARN(Render, "Swapchain out of date at acquire — recreating");
    m_Device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);
    backbuffer = m_Device->AcquireNextImage(packet.Swapchain);

    if (!backbuffer.IsValid())
        LOG_ERROR(Render, "Failed to acquire swapchain image after recreation — skipping frame");

    return backbuffer;
}

u32 RenderThread::UploadLights(const FramePacket& packet, const u32 frameSlot)
{
    const u32 lightCount = static_cast<u32>(packet.Lights.size());

    if (lightCount > m_LightSSBOCapacity)
    {
        m_Device->WaitIdle();
        u32 newCapacity = m_LightSSBOCapacity;
        while (newCapacity < lightCount)
            newCapacity *= 2;
        m_LightSSBOCapacity = newCapacity;

        BufferDesc desc{};
        desc.Size        = newCapacity * sizeof(GpuPointLight);
        desc.Usage       = BufferUsage::Storage;
        desc.HostVisible = true;
        desc.DebugName   = "light_ssbo";

        for (auto& ssbo : m_LightSSBO)
        {
            m_Device->DestroyBuffer(ssbo);
            ssbo = m_Device->CreateBuffer(desc);
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
        m_Device->UpdateBuffer(m_LightSSBO[frameSlot], gpuLights.data(), lightCount * sizeof(GpuPointLight));
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

    if (spriteCount > m_InstanceBufferCapacity)
    {
        m_Device->WaitIdle();
        u32 newCapacity = m_InstanceBufferCapacity;
        while (newCapacity < spriteCount)
            newCapacity *= 2;
        m_InstanceBufferCapacity = newCapacity;

        BufferDesc desc{};
        desc.Size        = newCapacity * sizeof(SpriteInstance);
        desc.Usage       = BufferUsage::Vertex;
        desc.HostVisible = true;
        desc.DebugName   = "sprite_instances";

        for (auto& buf : m_InstanceBuffers)
        {
            m_Device->DestroyBuffer(buf);
            buf = m_Device->CreateBuffer(desc);
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
    m_Device->UpdateBuffer(m_InstanceBuffers[frameSlot], instances.data(), spriteCount * sizeof(SpriteInstance));
}

void RenderThread::BeginRenderPass(const CommandListHandle cmd, const TextureHandle backbuffer, const FramePacket& packet) const
{
    Attachment colorAttach{};
    colorAttach.Texture    = backbuffer;
    colorAttach.Load       = LoadOp::Clear;
    colorAttach.Store      = StoreOp::Store;
    colorAttach.ClearColor = packet.ClearColor;

    std::array<Attachment, 1> attachments = { colorAttach };
    RenderingDesc             renderDesc{};
    renderDesc.ColorAttachments = attachments;
    m_Device->BeginRendering(cmd, renderDesc);
}

u32 RenderThread::DrawSpriteBatches(const CommandListHandle cmd, const std::vector<const Sprite*>& sorted, const u32 frameSlot,
                                    const FramePacket& packet, const u32 lightCount) const
{
    const u32 spriteCount = static_cast<u32>(sorted.size());
    if (spriteCount == 0)
        return 0;

    const f32 viewW = static_cast<f32>(packet.Width);
    const f32 viewH = static_cast<f32>(packet.Height);

    m_Device->BindPipeline(cmd, m_SpritePipeline);
    m_Device->SetViewport(cmd, 0.0f, 0.0f, viewW, viewH);
    m_Device->SetScissor(cmd, 0, 0, packet.Width, packet.Height);
    m_Device->BindStorageBuffer(cmd, m_LightSSBO[frameSlot], 2, 0);

    SpritePushConstants pc{};
    pc.CamPosX    = packet.CameraPosition.X;
    pc.CamPosY    = packet.CameraPosition.Y;
    pc.ViewportW  = viewW;
    pc.ViewportH  = viewH;
    pc.AmbientR   = packet.AmbientColor.R;
    pc.AmbientG   = packet.AmbientColor.G;
    pc.AmbientB   = packet.AmbientColor.B;
    pc.AmbientA   = packet.AmbientColor.A;
    pc.LightCount = lightCount;
    m_Device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment, &pc, sizeof(pc));

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
        m_Device->BindTexture(cmd, first->Texture, first->Sampler, 0);

        // Normal map — set 1. Fall back to the flat-normal default.
        const bool    hasNormal     = first->NormalTexture.IsValid();
        const TextureHandle normalTex     = hasNormal ? first->NormalTexture : m_DefaultNormalTex;
        const SamplerHandle normalSampler = hasNormal ? first->Sampler : m_DefaultNormalSampler;
        m_Device->BindTexture(cmd, normalTex, normalSampler, 1);

        // Offset into the instance buffer for this group's first sprite.
        m_Device->BindVertexBuffer(cmd, m_InstanceBuffers[frameSlot], 0, static_cast<u64>(groupStart) * sizeof(SpriteInstance));

        m_Device->Draw(cmd, 6, 0, groupEnd - groupStart, 0);
        ++batchCount;
        groupStart = groupEnd;
    }

    return batchCount;
}

void RenderThread::DrawLegacyCalls(const CommandListHandle cmd, const FramePacket& packet, const u32 frameSlot,
                                   const u32 lightCount) const
{
    const f32 viewW = static_cast<f32>(packet.Width);
    const f32 viewH = static_cast<f32>(packet.Height);

    for (const DrawCall& dc : packet.DrawCalls)
    {
        m_Device->BindPipeline(cmd, dc.Pipeline);
        m_Device->SetViewport(cmd, 0.0f, 0.0f, viewW, viewH);
        m_Device->SetScissor(cmd, 0, 0, packet.Width, packet.Height);
        m_Device->BindStorageBuffer(cmd, m_LightSSBO[frameSlot], 2, 0);

        if (dc.Texture.IsValid())
            m_Device->BindTexture(cmd, dc.Texture, dc.Sampler, 0);

        const bool    hasNormal     = dc.NormalTexture.IsValid();
        const TextureHandle normalTex     = hasNormal ? dc.NormalTexture : m_DefaultNormalTex;
        const SamplerHandle normalSampler = hasNormal ? dc.Sampler : m_DefaultNormalSampler;
        m_Device->BindTexture(cmd, normalTex, normalSampler, 1);

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
        m_Device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment, &fpc, sizeof(fpc));

        m_Device->Draw(cmd, dc.VertexCount);
    }
}

// ---------------------------------------------------------------------------
// RenderFrame — orchestrates one frame on the render thread
// ---------------------------------------------------------------------------

void RenderThread::RenderFrame(const FramePacket& packet)
{
    const TextureHandle backbuffer = AcquireBackbuffer(packet);
    if (!backbuffer.IsValid())
        return;

    const u32 frameSlot  = m_Device->GetCurrentFrameIndex(packet.Swapchain);
    const u32 lightCount = UploadLights(packet, frameSlot);

    const auto sorted = SortSprites(packet.Sprites);
    UploadInstances(sorted, frameSlot);

    CommandListHandle cmd = m_Device->BeginCommandList();
    BeginRenderPass(cmd, backbuffer, packet);

    const u32 batchCount = DrawSpriteBatches(cmd, sorted, frameSlot, packet, lightCount);
    DrawLegacyCalls(cmd, packet, frameSlot, lightCount);

    m_Device->EndRendering(cmd);

    m_StatSpritesSubmitted.store(static_cast<u32>(sorted.size()), std::memory_order_relaxed);
    m_StatSpriteBatches.store(batchCount, std::memory_order_relaxed);
    m_StatDrawCalls.store(static_cast<u32>(packet.DrawCalls.size()) + batchCount, std::memory_order_relaxed);
    m_StatLightsActive.store(lightCount, std::memory_order_relaxed);

    m_Device->EndCommandList(cmd);
    m_Device->Submit({ cmd });
    m_Device->Present(packet.Swapchain);
}

} // namespace Arcbit
