#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace Arcbit {

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
    f32 PosX,  PosY,  HalfW, HalfH;  // world-space center + half-extents (location 0)
    f32 U0,    V0,    U1,    V1;      // normalized UV rect                (location 1)
    f32 TintR, TintG, TintB, TintA;  // RGBA tint                         (location 2)
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
namespace {

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
// Lifetime
// ---------------------------------------------------------------------------

void RenderThread::Start(RenderDevice* device, Format swapchainFormat)
{
    m_Device = device;

    // --- Default flat-normal texture ----------------------------------------
    // (128, 128, 255, 255) → normalized (0.502, 0.502, 1.0, 1.0) in the shader,
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

    SamplerDesc normalSampDesc{};
    normalSampDesc.MinFilter  = Filter::Nearest;
    normalSampDesc.MagFilter  = Filter::Nearest;
    normalSampDesc.AddressU   = AddressMode::ClampToEdge;
    normalSampDesc.AddressV   = AddressMode::ClampToEdge;
    normalSampDesc.DebugName  = "default_normal_sampler";
    m_DefaultNormalSampler    = m_Device->CreateSampler(normalSampDesc);

    // --- Sprite pipeline ----------------------------------------------------
    // The pipeline owns the vertex layout (per-instance 3×vec4) and the
    // Forward+ descriptor set layout (albedo, normal, light SSBO).
    auto vertSpv = LoadShaderFile("shaders/sprite.vert.spv");
    auto fragSpv = LoadShaderFile("shaders/sprite.frag.spv");

    ShaderHandle vert = m_Device->CreateShader({
        ShaderStage::Vertex,
        vertSpv.data(), static_cast<u32>(vertSpv.size()),
        "main", "sprite.vert"
    });
    ShaderHandle frag = m_Device->CreateShader({
        ShaderStage::Fragment,
        fragSpv.data(), static_cast<u32>(fragSpv.size()),
        "main", "sprite.frag"
    });

    // Three vec4 instance attributes — each 16 bytes, packed in one binding.
    // PerInstance = true → VK_VERTEX_INPUT_RATE_INSTANCE (one entry per sprite).
    const VertexAttribute attrs[] = {
        { 0, 0, Format::RGBA32_Float,  0 },  // a_PosSize  (posX, posY, halfW, halfH)
        { 1, 0, Format::RGBA32_Float, 16 },  // a_UV       (u0, v0, u1, v1)
        { 2, 0, Format::RGBA32_Float, 32 },  // a_Tint     (r, g, b, a)
    };
    const VertexBinding bindings[] = {
        { 0, static_cast<u32>(sizeof(SpriteInstance)), true },  // per-instance
    };

    PipelineDesc spriteDesc{};
    spriteDesc.VertexShader     = vert;
    spriteDesc.FragmentShader   = frag;
    spriteDesc.Attributes       = attrs;
    spriteDesc.Bindings         = bindings;
    spriteDesc.CullMode         = CullMode::None;
    spriteDesc.ColorFormat      = swapchainFormat;
    spriteDesc.DepthFormat      = Format::Undefined;
    spriteDesc.UseTextures      = true;   // set 0 = albedo
    spriteDesc.UseNormalTexture = true;   // set 1 = normal map
    spriteDesc.UseStorageBuffer = true;   // set 2 = light SSBO
    // Standard premultiplied-alpha blend for sprite transparency.
    spriteDesc.Blend.Enable   = true;
    spriteDesc.Blend.SrcColor = BlendFactor::SrcAlpha;
    spriteDesc.Blend.DstColor = BlendFactor::OneMinusSrcAlpha;
    spriteDesc.Blend.ColorOp  = BlendOp::Add;
    spriteDesc.Blend.SrcAlpha = BlendFactor::One;
    spriteDesc.Blend.DstAlpha = BlendFactor::Zero;
    spriteDesc.Blend.AlphaOp  = BlendOp::Add;
    spriteDesc.DebugName      = "SpritePipeline";

    m_SpritePipeline = m_Device->CreatePipeline(spriteDesc);
    ARCBIT_ASSERT(m_SpritePipeline.IsValid(), "RenderThread: failed to create sprite pipeline");

    m_Device->DestroyShader(vert);
    m_Device->DestroyShader(frag);

    // --- Per-frame instance buffers -----------------------------------------
    // Pre-allocate capacity for 256 sprites; grows lazily in RenderFrame.
    static constexpr u32 InitialSpriteCapacity = 256;
    m_InstanceBufferCapacity = InitialSpriteCapacity;

    BufferDesc instanceDesc{};
    instanceDesc.Size        = InitialSpriteCapacity * sizeof(SpriteInstance);
    instanceDesc.Usage       = BufferUsage::Vertex;
    instanceDesc.HostVisible = true;
    instanceDesc.DebugName   = "sprite_instances";

    for (auto& buf : m_InstanceBuffers)
        buf = m_Device->CreateBuffer(instanceDesc);

    // --- Per-frame light SSBOs ----------------------------------------------
    static constexpr u32 InitialLightCapacity = 64;
    m_LightSSBOCapacity = InitialLightCapacity;

    BufferDesc ssboDesc{};
    ssboDesc.Size        = InitialLightCapacity * sizeof(GpuPointLight);
    ssboDesc.Usage       = BufferUsage::Storage;
    ssboDesc.HostVisible = true;
    ssboDesc.DebugName   = "light_ssbo";

    for (auto& ssbo : m_LightSSBO)
        ssbo = m_Device->CreateBuffer(ssboDesc);

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
        if (ssbo.IsValid()) m_Device->DestroyBuffer(ssbo);

    for (auto& buf : m_InstanceBuffers)
        if (buf.IsValid()) m_Device->DestroyBuffer(buf);

    if (m_SpritePipeline.IsValid())     m_Device->DestroyPipeline(m_SpritePipeline);
    if (m_DefaultNormalSampler.IsValid()) m_Device->DestroySampler(m_DefaultNormalSampler);
    if (m_DefaultNormalTex.IsValid())     m_Device->DestroyTexture(m_DefaultNormalTex);

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
        m_StatLegacyDrawCalls.load(std::memory_order_relaxed),
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
            if (!m_Running && !m_HasFrame) break;
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
// RenderFrame — single frame, render thread side
// ---------------------------------------------------------------------------

void RenderThread::RenderFrame(const FramePacket& packet)
{
    // --- Swapchain resize ---------------------------------------------------
    if (packet.NeedsResize)
        m_Device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);

    // --- Acquire backbuffer -------------------------------------------------
    TextureHandle backbuffer = m_Device->AcquireNextImage(packet.Swapchain);
    if (!backbuffer.IsValid())
    {
        LOG_WARN(Render, "Swapchain out of date at acquire — recreating");
        m_Device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);
        backbuffer = m_Device->AcquireNextImage(packet.Swapchain);
        if (!backbuffer.IsValid())
        {
            LOG_ERROR(Render, "Failed to acquire swapchain image after recreation — skipping frame");
            return;
        }
    }

    const u32 frameSlot = m_Device->GetCurrentFrameIndex(packet.Swapchain);

    // --- Light SSBO upload --------------------------------------------------
    const u32 lightCount = static_cast<u32>(packet.Lights.size());

    if (lightCount > m_LightSSBOCapacity)
    {
        m_Device->WaitIdle();

        u32 newCapacity = m_LightSSBOCapacity;
        while (newCapacity < lightCount) newCapacity *= 2;
        m_LightSSBOCapacity = newCapacity;

        BufferDesc ssboDesc{};
        ssboDesc.Size        = newCapacity * sizeof(GpuPointLight);
        ssboDesc.Usage       = BufferUsage::Storage;
        ssboDesc.HostVisible = true;
        ssboDesc.DebugName   = "light_ssbo";

        for (auto& ssbo : m_LightSSBO)
        {
            m_Device->DestroyBuffer(ssbo);
            ssbo = m_Device->CreateBuffer(ssboDesc);
        }
        LOG_INFO(Render, "Light SSBO resized to {} entries", newCapacity);
    }

    if (lightCount > 0)
    {
        std::vector<GpuPointLight> gpuLights;
        gpuLights.reserve(lightCount);
        for (const PointLight& light : packet.Lights)
        {
            GpuPointLight& g = gpuLights.emplace_back();
            g.PosX      = light.Position.X;
            g.PosY      = light.Position.Y;
            g.Radius    = light.Radius;
            g.Intensity = light.Intensity;
            g.ColorR    = light.LightColor.R;
            g.ColorG    = light.LightColor.G;
            g.ColorB    = light.LightColor.B;
            g.ColorA    = light.LightColor.A;
        }
        m_Device->UpdateBuffer(m_LightSSBO[frameSlot], gpuLights.data(),
                               lightCount * sizeof(GpuPointLight));
    }

    // --- Sprite instance upload ---------------------------------------------
    // Sort sprites: primary key = layer (painter's algorithm), secondary = texture
    // handle (groups same-texture sprites to minimize descriptor switches).
    std::vector<const Sprite*> sorted;
    sorted.reserve(packet.Sprites.size());
    for (const Sprite& s : packet.Sprites)
        sorted.push_back(&s);

    std::stable_sort(sorted.begin(), sorted.end(), [](const Sprite* a, const Sprite* b)
    {
        if (a->Layer != b->Layer) return a->Layer < b->Layer;
        // Within the same layer, group by texture then sampler to minimize binds.
        if (a->Texture.Index() != b->Texture.Index())
            return a->Texture.Index() < b->Texture.Index();
        return a->Sampler.Index() < b->Sampler.Index();
    });

    const u32 spriteCount = static_cast<u32>(sorted.size());

    if (spriteCount > m_InstanceBufferCapacity)
    {
        m_Device->WaitIdle();

        u32 newCapacity = m_InstanceBufferCapacity;
        while (newCapacity < spriteCount) newCapacity *= 2;
        m_InstanceBufferCapacity = newCapacity;

        BufferDesc instanceDesc{};
        instanceDesc.Size        = newCapacity * sizeof(SpriteInstance);
        instanceDesc.Usage       = BufferUsage::Vertex;
        instanceDesc.HostVisible = true;
        instanceDesc.DebugName   = "sprite_instances";

        for (auto& buf : m_InstanceBuffers)
        {
            m_Device->DestroyBuffer(buf);
            buf = m_Device->CreateBuffer(instanceDesc);
        }
        LOG_INFO(Render, "Instance buffer resized to {} sprites", newCapacity);
    }

    if (spriteCount > 0)
    {
        std::vector<SpriteInstance> instances;
        instances.reserve(spriteCount);
        for (const Sprite* s : sorted)
        {
            SpriteInstance& inst = instances.emplace_back();
            inst.PosX  = s->Position.X;
            inst.PosY  = s->Position.Y;
            inst.HalfW = s->Size.X * 0.5f;
            inst.HalfH = s->Size.Y * 0.5f;
            inst.U0    = s->UV.U0;
            inst.V0    = s->UV.V0;
            inst.U1    = s->UV.U1;
            inst.V1    = s->UV.V1;
            inst.TintR = s->Tint.R;
            inst.TintG = s->Tint.G;
            inst.TintB = s->Tint.B;
            inst.TintA = s->Tint.A;
        }
        m_Device->UpdateBuffer(m_InstanceBuffers[frameSlot], instances.data(),
                               spriteCount * sizeof(SpriteInstance));
    }

    // --- Command recording --------------------------------------------------
    CommandListHandle cmd = m_Device->BeginCommandList();

    Attachment colorAttach{};
    colorAttach.Texture    = backbuffer;
    colorAttach.Load       = LoadOp::Clear;
    colorAttach.Store      = StoreOp::Store;
    colorAttach.ClearColor = packet.ClearColor;

    std::array<Attachment, 1> attachments = { colorAttach };
    RenderingDesc renderDesc{};
    renderDesc.ColorAttachments = attachments;
    m_Device->BeginRendering(cmd, renderDesc);

    const f32 viewW = static_cast<f32>(packet.Width);
    const f32 viewH = static_cast<f32>(packet.Height);

    // --- Sprite batch draws -------------------------------------------------
    if (spriteCount > 0)
    {
        m_Device->BindPipeline(cmd, m_SpritePipeline);
        m_Device->SetViewport(cmd, 0.0f, 0.0f, viewW, viewH);
        m_Device->SetScissor(cmd, 0, 0, packet.Width, packet.Height);

        // Push constants are the same for all sprite groups this frame.
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
        m_Device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment,
                                &pc, sizeof(pc));

        // Bind the light SSBO once — it doesn't change between groups.
        m_Device->BindStorageBuffer(cmd, m_LightSSBO[frameSlot], 2, 0);

        // Walk through the sorted sprite list and emit one draw per texture group.
        u32 batchCount = 0;
        u32 groupStart = 0;
        while (groupStart < spriteCount)
        {
            const Sprite* first   = sorted[groupStart];
            u32           groupEnd = groupStart + 1;

            // Extend the group as long as texture + sampler + normal map match.
            while (groupEnd < spriteCount)
            {
                const Sprite* next = sorted[groupEnd];
                if (next->Texture.Index()       != first->Texture.Index()       ||
                    next->Sampler.Index()        != first->Sampler.Index()       ||
                    next->NormalTexture.Index()  != first->NormalTexture.Index())
                    break;
                ++groupEnd;
            }

            const u32 groupSize = groupEnd - groupStart;

            // Albedo — set 0.
            m_Device->BindTexture(cmd, first->Texture, first->Sampler, 0);

            // Normal map — set 1. Fall back to the flat-normal default.
            TextureHandle normalTex    = first->NormalTexture.IsValid()
                                         ? first->NormalTexture : m_DefaultNormalTex;
            SamplerHandle normalSampler = first->NormalTexture.IsValid()
                                         ? first->Sampler : m_DefaultNormalSampler;
            m_Device->BindTexture(cmd, normalTex, normalSampler, 1);

            // Instance buffer slice: byte offset = groupStart * sizeof(SpriteInstance).
            m_Device->BindVertexBuffer(cmd, m_InstanceBuffers[frameSlot], 0,
                                       static_cast<u64>(groupStart) * sizeof(SpriteInstance));

            // 6 vertices (two triangles) × groupSize instances.
            m_Device->Draw(cmd, 6, 0, groupSize, 0);

            ++batchCount;
            groupStart = groupEnd;
        }

        m_StatSpriteBatches.store(batchCount, std::memory_order_relaxed);
    }
    else
    {
        m_StatSpriteBatches.store(0, std::memory_order_relaxed);
    }

    // --- Legacy DrawCall draws (after sprites, in submission order) ---------
    for (const DrawCall& dc : packet.DrawCalls)
    {
        m_Device->BindPipeline(cmd, dc.Pipeline);
        m_Device->SetViewport(cmd, 0.0f, 0.0f, viewW, viewH);
        m_Device->SetScissor(cmd, 0, 0, packet.Width, packet.Height);

        if (dc.Texture.IsValid())
            m_Device->BindTexture(cmd, dc.Texture, dc.Sampler, 0);

        TextureHandle normalTex    = dc.NormalTexture.IsValid() ? dc.NormalTexture : m_DefaultNormalTex;
        SamplerHandle normalSampler = dc.NormalTexture.IsValid() ? dc.Sampler : m_DefaultNormalSampler;
        m_Device->BindTexture(cmd, normalTex, normalSampler, 1);

        m_Device->BindStorageBuffer(cmd, m_LightSSBO[frameSlot], 2, 0);

        ForwardPushConstants fpc{};
        fpc.PositionX = dc.Position.X;
        fpc.PositionY = dc.Position.Y;
        fpc.ScaleX    = dc.Scale.X;
        fpc.ScaleY    = dc.Scale.Y;
        fpc.U0        = dc.UV.U0;
        fpc.V0        = dc.UV.V0;
        fpc.U1        = dc.UV.U1;
        fpc.V1        = dc.UV.V1;
        fpc.AmbientR  = packet.AmbientColor.R;
        fpc.AmbientG  = packet.AmbientColor.G;
        fpc.AmbientB  = packet.AmbientColor.B;
        fpc.AmbientA  = packet.AmbientColor.A;
        fpc.LightCount = lightCount;
        m_Device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment,
                                &fpc, sizeof(fpc));

        m_Device->Draw(cmd, dc.VertexCount);
    }

    m_Device->EndRendering(cmd);

    // Store per-frame stats (read by game thread via GetStats()).
    m_StatSpritesSubmitted.store(spriteCount,                                 std::memory_order_relaxed);
    m_StatLegacyDrawCalls.store(static_cast<u32>(packet.DrawCalls.size()),   std::memory_order_relaxed);
    m_StatLightsActive.store(lightCount,                                      std::memory_order_relaxed);

    m_Device->EndCommandList(cmd);
    m_Device->Submit({ cmd });
    m_Device->Present(packet.Swapchain);
}

} // namespace Arcbit
