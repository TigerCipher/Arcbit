#pragma once

#include <arcbit/render/RenderTypes.h>

#include <initializer_list>
#include <memory>

namespace Arcbit {

class VulkanContext;   // Pimpl — defined in backend, never included by engine/game code

// ---------------------------------------------------------------------------
// RenderDevice
//
// The single point of contact between game code and the GPU.
// Created by the backend DLL via Arcbit_CreateDevice.
// Destroyed via Arcbit_DestroyDevice.
//
// All GPU resources are identified by typed handles (u32).
// Vulkan types never appear outside the backend DLL.
// ---------------------------------------------------------------------------
class ARCBIT_RENDER_API RenderDevice
{
public:
    ~RenderDevice();

    // Non-copyable — one device per application.
    RenderDevice(const RenderDevice&)            = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;

    // --- Buffers -----------------------------------------------------------
    BufferHandle CreateBuffer(const BufferDesc& desc);
    // Upload CPU data into an existing buffer.
    // For HostVisible buffers this is a direct memcpy.
    // For device-local buffers this goes through a staging buffer.
    void         UpdateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset = 0);
    void         DestroyBuffer(BufferHandle handle);

    // --- Textures ----------------------------------------------------------
    TextureHandle CreateTexture(const TextureDesc& desc);
    void          UploadTexture(TextureHandle handle, const void* data, u64 size);
    void          DestroyTexture(TextureHandle handle);

    // --- Samplers ----------------------------------------------------------
    SamplerHandle CreateSampler(const SamplerDesc& desc);
    void          DestroySampler(SamplerHandle handle);

    // --- Shaders -----------------------------------------------------------
    ShaderHandle  CreateShader(const ShaderDesc& desc);
    void          DestroyShader(ShaderHandle handle);

    // --- Pipelines ---------------------------------------------------------
    PipelineHandle CreatePipeline(const PipelineDesc& desc);
    void           DestroyPipeline(PipelineHandle handle);

    // --- Swapchain ---------------------------------------------------------
    SwapchainHandle CreateSwapchain(const SwapchainDesc& desc);
    void            ResizeSwapchain(SwapchainHandle handle, u32 width, u32 height);
    void            DestroySwapchain(SwapchainHandle handle);

    // --- Frame -------------------------------------------------------------

    // Call at the start of each frame. Acquires the next swapchain image
    // and returns it as a TextureHandle usable as a render attachment.
    TextureHandle AcquireNextImage(SwapchainHandle handle);

    // --- Commands ----------------------------------------------------------
    CommandListHandle BeginCommandList();
    void              EndCommandList(CommandListHandle handle);

    // Dynamic rendering — no VkRenderPass objects.
    // Matches Vulkan 1.3 vkCmdBeginRendering.
    void BeginRendering(CommandListHandle cmd, const RenderingDesc& desc);
    void EndRendering(CommandListHandle cmd);

    void BindPipeline(CommandListHandle cmd, PipelineHandle pipeline);

    void BindVertexBuffer(CommandListHandle cmd, BufferHandle buffer,
                          u32 binding, u64 offset = 0);

    void BindIndexBuffer(CommandListHandle cmd, BufferHandle buffer,
                         IndexType type, u64 offset = 0);

    // Bind a storage buffer (SSBO) — used for the dynamic light list etc.
    void BindStorageBuffer(CommandListHandle cmd, BufferHandle buffer,
                           u32 set, u32 binding);

    // Push constants — small, fast per-draw data (transform, light count, etc.)
    void PushConstants(CommandListHandle cmd, ShaderStage stages,
                       const void* data, u32 size, u32 offset = 0);

    void SetViewport(CommandListHandle cmd, f32 x, f32 y, f32 width, f32 height,
                     f32 minDepth = 0.0f, f32 maxDepth = 1.0f);

    void SetScissor(CommandListHandle cmd, i32 x, i32 y, u32 width, u32 height);

    void Draw(CommandListHandle cmd, u32 vertexCount, u32 firstVertex = 0,
              u32 instanceCount = 1, u32 firstInstance = 0);

    void DrawIndexed(CommandListHandle cmd, u32 indexCount, u32 firstIndex = 0,
                     i32 vertexOffset = 0, u32 instanceCount = 1, u32 firstInstance = 0);

    // --- Submit & present --------------------------------------------------
    void Submit(std::initializer_list<CommandListHandle> commands);
    void Present(SwapchainHandle handle);

    // Block until all GPU work is complete.
    // Use only during shutdown or resource rebuilds.
    void WaitIdle();

    // Constructed by Arcbit_CreateDevice only.
    // VulkanContext is forward-declared and never defined in engine headers,
    // so game code cannot call this constructor even though it is public.
    explicit RenderDevice(std::unique_ptr<VulkanContext> context);

private:
    std::unique_ptr<VulkanContext> m_Context;
};

} // namespace Arcbit

// ---------------------------------------------------------------------------
// Backend factory — C linkage so the symbol is stable across the DLL boundary.
// extern "C" cannot live inside a namespace — these are global symbols.
// Call Arcbit_CreateDevice once at startup.
// Call Arcbit_DestroyDevice before SDL_DestroyWindow.
// ---------------------------------------------------------------------------
extern "C"
{
    ARCBIT_RENDER_API Arcbit::RenderDevice* Arcbit_CreateDevice(const Arcbit::DeviceDesc& desc);
    ARCBIT_RENDER_API void                  Arcbit_DestroyDevice(Arcbit::RenderDevice* device);
}
