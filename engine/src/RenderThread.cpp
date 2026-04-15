#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Log.h>

#include <array>
#include <vector>

namespace Arcbit {

// GPU-side point light struct. Must match the GLSL std430 layout in forward.frag:
//   struct PointLight { vec2 position; float radius; float intensity; vec4 color; };
// 32 bytes, naturally aligned — no padding required.
struct GpuPointLight
{
    f32 PosX, PosY;                          // NDC position
    f32 Radius;
    f32 Intensity;
    f32 ColorR, ColorG, ColorB, ColorA;
};

// Push constant block. Must match the layout(push_constant) block in both shaders.
//   vec2 position  — offset  0
//   vec2 scale     — offset  8
//   vec4 uv        — offset 16  (u0, v0, u1, v1)
//   vec4 ambient   — offset 32  (r, g, b, a)
//   uint lightCount — offset 48
struct ForwardPushConstants
{
    f32 PositionX, PositionY;
    f32 ScaleX, ScaleY;
    f32 U0, V0, U1, V1;
    f32 AmbientR, AmbientG, AmbientB, AmbientA;
    u32 LightCount;
};

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

void RenderThread::Start(RenderDevice* device)
{
    m_Device  = device;

    // --- Default flat-normal texture ----------------------------------------
    //
    // (0.5, 0.5, 1.0) encodes a normal pointing straight out of the screen in
    // tangent space. Used as a fallback when a DrawCall has no NormalTexture,
    // so the lighting math still works without a branch in the shader.
    TextureDesc normalDesc{};
    normalDesc.Width     = 1;
    normalDesc.Height    = 1;
    normalDesc.Format    = Format::RGBA8_UNorm;
    normalDesc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
    normalDesc.DebugName = "default_normal";

    m_DefaultNormalTex = m_Device->CreateTexture(normalDesc);

    // RGBA8 flat normal: R=128, G=128, B=255, A=255 → (0.502, 0.502, 1.0, 1.0)
    const u8 flatNormal[4] = { 128, 128, 255, 255 };
    m_Device->UploadTexture(m_DefaultNormalTex, flatNormal, sizeof(flatNormal));

    // Nearest-neighbor sampler — the 1×1 texture doesn't benefit from filtering.
    SamplerDesc samplerDesc{};
    samplerDesc.MinFilter  = Filter::Nearest;
    samplerDesc.MagFilter  = Filter::Nearest;
    samplerDesc.AddressU   = AddressMode::ClampToEdge;
    samplerDesc.AddressV   = AddressMode::ClampToEdge;
    samplerDesc.DebugName  = "default_normal_sampler";

    m_DefaultNormalSampler = m_Device->CreateSampler(samplerDesc);

    // --- Per-frame light SSBOs -----------------------------------------------
    //
    // Pre-allocate one SSBO per frame-in-flight slot sized for 64 lights.
    // They grow lazily if the scene submits more lights (see RenderFrame).
    static constexpr u32 InitialLightCapacity = 64;
    m_LightSSBOCapacity = InitialLightCapacity;

    BufferDesc ssboDesc{};
    ssboDesc.Size        = InitialLightCapacity * sizeof(GpuPointLight);
    ssboDesc.Usage       = BufferUsage::Storage;
    ssboDesc.HostVisible = true;
    ssboDesc.DebugName   = "light_ssbo";

    for (auto& ssbo : m_LightSSBO)
        ssbo = m_Device->CreateBuffer(ssboDesc);

    // Set m_Running before spawning the thread so the thread never sees false
    // on first entry into Run().
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

    // The render thread has exited, but the GPU may still be executing the last
    // submitted frame. WaitIdle blocks until all GPU work is complete so the
    // resources below can be safely destroyed.
    m_Device->WaitIdle();

    // Destroy forward+ resources — GPU is now idle, safe to free descriptors.
    for (auto& ssbo : m_LightSSBO)
    {
        if (ssbo.IsValid()) m_Device->DestroyBuffer(ssbo);
    }

    if (m_DefaultNormalSampler.IsValid()) m_Device->DestroySampler(m_DefaultNormalSampler);
    if (m_DefaultNormalTex.IsValid())     m_Device->DestroyTexture(m_DefaultNormalTex);

    LOG_INFO(Render, "Render thread stopped");
}

// ---------------------------------------------------------------------------
// Frame submission (game thread side)
// ---------------------------------------------------------------------------

void RenderThread::SubmitFrame(FramePacket packet)
{
    std::unique_lock lock(m_Mutex);

    // Back-pressure: wait until the render thread has finished the previous frame.
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
// Single-frame rendering (render thread side) — Forward+ pass
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
        LOG_WARN(Render, "Swapchain out of date at acquire — recreating and retrying");
        m_Device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);
        backbuffer = m_Device->AcquireNextImage(packet.Swapchain);

        if (!backbuffer.IsValid())
        {
            LOG_ERROR(Render, "Failed to acquire image after swapchain recreation — skipping frame");
            return;
        }
    }

    // --- Light SSBO upload --------------------------------------------------
    //
    // Convert PointLight entries to the GPU-side struct layout and upload to
    // the SSBO slot for this frame. If the scene exceeds current capacity,
    // recreate all SSBO slots at the new size (WaitIdle ensures the GPU is
    // not reading from any slot).
    const u32 lightCount = static_cast<u32>(packet.Lights.size());

    if (lightCount > m_LightSSBOCapacity)
    {
        m_Device->WaitIdle();

        // Grow to next power-of-two above lightCount (simple doubling strategy).
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

    // Build the CPU-side GpuPointLight array and upload for this frame slot.
    const u32 frameSlot = m_Device->GetCurrentFrameIndex(packet.Swapchain);
    BufferHandle currentSSBO = m_LightSSBO[frameSlot];

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

        m_Device->UpdateBuffer(currentSSBO, gpuLights.data(),
            lightCount * sizeof(GpuPointLight));
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

    for (const DrawCall& dc : packet.DrawCalls)
    {
        m_Device->BindPipeline(cmd, dc.Pipeline);

        m_Device->SetViewport(cmd, 0.0f, 0.0f,
            static_cast<f32>(packet.Width),
            static_cast<f32>(packet.Height));
        m_Device->SetScissor(cmd, 0, 0, packet.Width, packet.Height);

        // Albedo texture — set 0.
        if (dc.Texture.IsValid())
            m_Device->BindTexture(cmd, dc.Texture, dc.Sampler, 0);

        // Normal map — set 1. Fall back to the default flat-normal texture when
        // the draw call has no normal map, so the shader can run identically.
        TextureHandle normalTex    = dc.NormalTexture.IsValid() ? dc.NormalTexture : m_DefaultNormalTex;
        SamplerHandle normalSampler = dc.NormalTexture.IsValid() ? dc.Sampler : m_DefaultNormalSampler;
        m_Device->BindTexture(cmd, normalTex, normalSampler, 1);

        // Light SSBO — set 2.
        m_Device->BindStorageBuffer(cmd, currentSSBO, 2, 0);

        // Push constants: position, scale, UV rect, ambient color, light count.
        ForwardPushConstants pc{};
        pc.PositionX = dc.Position.X;
        pc.PositionY = dc.Position.Y;
        pc.ScaleX    = dc.Scale.X;
        pc.ScaleY    = dc.Scale.Y;
        pc.U0        = dc.UV.U0;
        pc.V0        = dc.UV.V0;
        pc.U1        = dc.UV.U1;
        pc.V1        = dc.UV.V1;
        pc.AmbientR  = packet.AmbientColor.R;
        pc.AmbientG  = packet.AmbientColor.G;
        pc.AmbientB  = packet.AmbientColor.B;
        pc.AmbientA  = packet.AmbientColor.A;
        pc.LightCount = lightCount;

        m_Device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment,
            &pc, sizeof(pc));

        m_Device->Draw(cmd, dc.VertexCount);
    }

    m_Device->EndRendering(cmd);

    // --- Submit and present -------------------------------------------------
    m_Device->EndCommandList(cmd);
    m_Device->Submit({ cmd });
    m_Device->Present(packet.Swapchain);
}

} // namespace Arcbit
