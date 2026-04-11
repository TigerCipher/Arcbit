#include <arcbit/render/RenderDevice.h>
#include "VulkanContext.h"

#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// RenderDevice — construction / destruction
// ---------------------------------------------------------------------------

// Takes ownership of the fully-initialised VulkanContext via unique_ptr.
// Construction is only reachable from Arcbit_CreateDevice because VulkanContext
// is an incomplete type in all headers outside this backend DLL.
RenderDevice::RenderDevice(std::unique_ptr<VulkanContext> context)
    : m_Context(std::move(context))
{}

// Destructor delegates to VulkanContext::Shutdown which destroys all Vulkan
// objects in reverse-init order. Callers should call WaitIdle() first to
// ensure no GPU work is still in flight.
RenderDevice::~RenderDevice()
{
    if (m_Context)
        m_Context->Shutdown();
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

// Allocates a GPU buffer and registers it in the handle pool.
//
// Memory strategy:
//   HostVisible = true  → VMA_MEMORY_USAGE_CPU_TO_GPU
//     Allocated in host-visible memory (often write-combined system RAM on desktop).
//     VMA maps it immediately and keeps it mapped persistently (VMA_ALLOCATION_CREATE_MAPPED_BIT).
//     CPU writes go directly to the buffer — no staging required.
//     Used for: per-frame uniform buffers, dynamic SSBO light lists.
//
//   HostVisible = false → VMA_MEMORY_USAGE_GPU_ONLY
//     Allocated in device-local VRAM — the fastest memory for GPU reads.
//     CPU cannot write to it directly; must use a staging buffer (see UpdateBuffer).
//     Used for: static vertex buffers, index buffers, read-only textures.
//
// Debug naming:
//   If desc.DebugName is set and the validation extension is present, the VkBuffer
//   gets a human-readable name that appears in RenderDoc, Nsight, and validation messages.
BufferHandle RenderDevice::CreateBuffer(const BufferDesc& desc)
{
    ARCBIT_ASSERT(desc.Size > 0, "Buffer size must be > 0");

    // Build the Vulkan buffer creation info from our engine-side description.
    // VK_SHARING_MODE_EXCLUSIVE means only one queue family accesses this buffer
    // at a time — no ownership transfer barriers needed for the common single-queue case.
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size        = desc.Size;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Map our BufferUsage flags to Vulkan's VkBufferUsageFlags.
    // Multiple flags can be combined — e.g. Transfer on a staging buffer,
    // or Vertex | Transfer on a buffer that's initially uploaded via staging.
    if (HasFlag(desc.Usage, BufferUsage::Vertex))   bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Index))    bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Uniform))  bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Storage))  bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    // Transfer adds both SRC and DST so this buffer can be used on either end of a copy.
    if (HasFlag(desc.Usage, BufferUsage::Transfer)) bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                                                                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // Tell VMA how to choose a memory heap for this allocation.
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = desc.HostVisible
        ? VMA_MEMORY_USAGE_CPU_TO_GPU   // host-visible, suitable for streaming data to the GPU
        : VMA_MEMORY_USAGE_GPU_ONLY;    // device-local VRAM — fastest for GPU reads

    // Persistently map host-visible buffers so we can memcpy into them without
    // calling vkMapMemory every frame. VMA keeps the mapping alive for the buffer's lifetime.
    if (desc.HostVisible)
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    // Prepare the internal resource struct that will live in the pool.
    VulkanBuffer vkBuffer{};
    vkBuffer.Size        = desc.Size;
    vkBuffer.Usage       = desc.Usage;
    vkBuffer.HostVisible = desc.HostVisible;

    // Create the buffer and its backing memory allocation in one call.
    // VMA selects the appropriate heap and suballocates within a larger block.
    VmaAllocationInfo allocResult{};
    const VkResult result = vmaCreateBuffer(
        m_Context->Allocator,
        &bufferInfo, &allocInfo,
        &vkBuffer.Buffer, &vkBuffer.Allocation,
        &allocResult);

    ARCBIT_VERIFY(result == VK_SUCCESS, "vmaCreateBuffer failed");

    // Store the persistent CPU pointer for host-visible buffers.
    // pMappedData is filled by VMA when VMA_ALLOCATION_CREATE_MAPPED_BIT is set.
    if (desc.HostVisible)
        vkBuffer.Mapped = allocResult.pMappedData;

    // Optionally assign a debug name — visible in Vulkan tools (RenderDoc, Nsight).
    // vkSetDebugUtilsObjectNameEXT is an extension function; it must be loaded at runtime.
    if (desc.DebugName)
    {
        VkDebugUtilsObjectNameInfoEXT nameInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType   = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<u64>(vkBuffer.Buffer);
        nameInfo.pObjectName  = desc.DebugName;

        auto fn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(m_Context->Device, "vkSetDebugUtilsObjectNameEXT"));
        if (fn) fn(m_Context->Device, &nameInfo);
    }

    // Register the buffer in the pool and return a typed handle.
    // The handle encodes the pool index and a generation counter for stale-handle detection.
    return m_Context->Buffers.Allocate<BufferTag>(std::move(vkBuffer));
}

// Writes CPU data into a previously allocated buffer.
//
// Two paths depending on memory type:
//
//   Host-visible (uniform buffers, dynamic SSBOs):
//     The buffer is persistently mapped. We memcpy directly into it.
//     Fast — no GPU round-trip, no synchronisation needed for the copy itself.
//     The GPU sees the new data on the next queue submit.
//
//   Device-local (vertex/index buffers):
//     The GPU cannot see host memory directly. We must:
//       1. Create a temporary host-visible staging buffer.
//       2. memcpy the data into the staging buffer.
//       3. Record and immediately submit a vkCmdCopyBuffer command.
//       4. vkQueueWaitIdle to block until the copy is done.
//       5. Destroy the staging buffer.
//     This is slower but device-local memory is 2-5x faster in shader reads.
//     For static geometry this cost is paid once at load time.
void RenderDevice::UpdateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset)
{
    VulkanBuffer* buf = m_Context->Buffers.Get(handle);
    ARCBIT_ASSERT(buf != nullptr, "UpdateBuffer: invalid handle");
    ARCBIT_ASSERT(offset + size <= buf->Size, "UpdateBuffer: write out of bounds");

    if (buf->HostVisible)
    {
        // Fast path: the buffer is persistently mapped — write directly.
        memcpy(static_cast<u8*>(buf->Mapped) + offset, data, size);
    }
    else
    {
        // Slow path: device-local buffer — must go via a staging buffer.

        // 1. Create a temporary host-visible transfer source buffer.
        BufferDesc stagingDesc{};
        stagingDesc.Size        = size;
        stagingDesc.Usage       = BufferUsage::Transfer;
        stagingDesc.HostVisible = true;
        stagingDesc.DebugName   = "staging";

        BufferHandle stagingHandle = CreateBuffer(stagingDesc);
        VulkanBuffer* staging = m_Context->Buffers.Get(stagingHandle);

        // 2. Copy the CPU data into the staging buffer.
        memcpy(staging->Mapped, data, size);

        // 3. Allocate a one-shot command buffer for the GPU copy.
        //    ONE_TIME_SUBMIT_BIT tells the driver this buffer will be submitted once
        //    and discarded — it may enable driver-side optimisations.
        VkCommandBufferAllocateInfo cmdAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdAlloc.commandPool        = m_Context->CommandPool;
        cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(m_Context->Device, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Record: copy `size` bytes from staging → destination, starting at `offset` in the dst.
        VkBufferCopy region{ 0, offset, size };
        vkCmdCopyBuffer(cmd, staging->Buffer, buf->Buffer, 1, &region);

        vkEndCommandBuffer(cmd);

        // 4. Submit and wait — we block the CPU here because the staging buffer
        //    must remain alive until the GPU copy finishes.
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        vkQueueSubmit(m_Context->GraphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GraphicsQueue);

        // 5. Free the temporary command buffer and staging buffer.
        vkFreeCommandBuffers(m_Context->Device, m_Context->CommandPool, 1, &cmd);
        DestroyBuffer(stagingHandle);
    }
}

// Releases the GPU buffer and returns its memory to VMA's pool.
// The handle is invalidated (its generation slot is bumped) so any remaining
// copies of the handle are detected as stale on next use.
void RenderDevice::DestroyBuffer(BufferHandle handle)
{
    // Free() moves the resource out of the pool and bumps the generation counter.
    auto resource = m_Context->Buffers.Free(handle);
    if (!resource) return; // handle was already invalid or double-freed

    // vmaDestroyBuffer destroys both the VkBuffer and its VmaAllocation in one call.
    if (resource->Buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_Context->Allocator, resource->Buffer, resource->Allocation);
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Compiles a SPIR-V shader module on the device.
//
// SPIR-V is Vulkan's binary intermediate representation — all shaders must be
// pre-compiled from GLSL/HLSL to SPIR-V before being passed here. We do this
// at build time with glslc (part of the Vulkan SDK).
//
// The driver copies the bytecode into its own memory, so the caller's array
// can be freed immediately after this call returns.
//
// The SPIR-V spec requires 4-byte alignment on the bytecode pointer and size
// (VkShaderModuleCreateInfo::pCode is a const uint32_t*).
ShaderHandle RenderDevice::CreateShader(const ShaderDesc& desc)
{
    ARCBIT_ASSERT(desc.Code != nullptr && desc.CodeSize > 0, "Shader code must not be empty");
    ARCBIT_ASSERT(desc.CodeSize % 4 == 0, "SPIR-V bytecode must be 4-byte aligned");

    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = desc.CodeSize;
    // Vulkan expects uint32_t* — the bytecode is always DWORD-aligned (asserted above).
    info.pCode    = reinterpret_cast<const u32*>(desc.Code);

    VulkanShader vkShader{};
    vkShader.Stage = desc.Stage; // stored so the pipeline knows which stage this module belongs to

    const VkResult result = vkCreateShaderModule(m_Context->Device, &info, nullptr, &vkShader.Module);
    ARCBIT_VERIFY(result == VK_SUCCESS, "vkCreateShaderModule failed");

    return m_Context->Shaders.Allocate<ShaderTag>(std::move(vkShader));
}

// Destroys the shader module.
// Pipelines that were built using this module are unaffected — Vulkan copies
// the SPIR-V bytecode during pipeline creation, so the module can be safely
// destroyed immediately after CreatePipeline returns.
void RenderDevice::DestroyShader(ShaderHandle handle)
{
    auto resource = m_Context->Shaders.Free(handle);
    if (!resource) return;
    if (resource->Module != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_Context->Device, resource->Module, nullptr);
}

// ---------------------------------------------------------------------------
// Stubs — implemented in subsequent sessions
// ---------------------------------------------------------------------------
TextureHandle RenderDevice::CreateTexture(const TextureDesc&)      { return TextureHandle::Invalid(); }
void          RenderDevice::UploadTexture(TextureHandle, const void*, u64) {}
void          RenderDevice::DestroyTexture(TextureHandle)          {}

SamplerHandle RenderDevice::CreateSampler(const SamplerDesc&)      { return SamplerHandle::Invalid(); }
void          RenderDevice::DestroySampler(SamplerHandle)          {}

PipelineHandle RenderDevice::CreatePipeline(const PipelineDesc&)   { return PipelineHandle::Invalid(); }
void           RenderDevice::DestroyPipeline(PipelineHandle)       {}

SwapchainHandle RenderDevice::CreateSwapchain(const SwapchainDesc&){ return SwapchainHandle::Invalid(); }
void            RenderDevice::ResizeSwapchain(SwapchainHandle, u32, u32) {}
void            RenderDevice::DestroySwapchain(SwapchainHandle)    {}

TextureHandle   RenderDevice::AcquireNextImage(SwapchainHandle)    { return TextureHandle::Invalid(); }

CommandListHandle RenderDevice::BeginCommandList()                 { return CommandListHandle::Invalid(); }
void              RenderDevice::EndCommandList(CommandListHandle)  {}

void RenderDevice::BeginRendering(CommandListHandle, const RenderingDesc&) {}
void RenderDevice::EndRendering(CommandListHandle)                         {}
void RenderDevice::BindPipeline(CommandListHandle, PipelineHandle)         {}
void RenderDevice::BindVertexBuffer(CommandListHandle, BufferHandle, u32, u64) {}
void RenderDevice::BindIndexBuffer(CommandListHandle, BufferHandle, IndexType, u64) {}
void RenderDevice::BindStorageBuffer(CommandListHandle, BufferHandle, u32, u32) {}
void RenderDevice::PushConstants(CommandListHandle, ShaderStage, const void*, u32, u32) {}
void RenderDevice::SetViewport(CommandListHandle, f32, f32, f32, f32, f32, f32) {}
void RenderDevice::SetScissor(CommandListHandle, i32, i32, u32, u32) {}
void RenderDevice::Draw(CommandListHandle, u32, u32, u32, u32)  {}
void RenderDevice::DrawIndexed(CommandListHandle, u32, u32, i32, u32, u32) {}
void RenderDevice::Submit(std::initializer_list<CommandListHandle>) {}
void RenderDevice::Present(SwapchainHandle)                        {}

// Block the calling thread until all pending GPU work on this device has finished.
// Equivalent to vkDeviceWaitIdle — drains all queues completely.
// Use only during shutdown or when rebuilding resources that the GPU is currently using.
// Prefer fine-grained per-frame fences for normal frame pacing.
void RenderDevice::WaitIdle()
{
    if (m_Context && m_Context->Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_Context->Device);
}

} // namespace Arcbit

// ---------------------------------------------------------------------------
// Factory functions
//
// These must be outside namespace Arcbit because C linkage (extern "C") and
// C++ namespaces are incompatible — a C symbol has no concept of namespacing.
// We use the Arcbit_ prefix instead to avoid name collisions.
//
// Arcbit_CreateDevice:
//   Constructs and initialises the Vulkan backend. Returns nullptr if any
//   step fails — the caller should check and exit cleanly.
//
// Arcbit_DestroyDevice:
//   Calls the destructor via delete. The destructor calls WaitIdle internally
//   via VulkanContext::Shutdown, but callers should still call WaitIdle()
//   beforehand if GPU work may still be in flight.
// ---------------------------------------------------------------------------
extern "C"
{

ARCBIT_RENDER_API Arcbit::RenderDevice* Arcbit_CreateDevice(const Arcbit::DeviceDesc& desc)
{
    // Construct the opaque backend context that holds all Vulkan state.
    auto context = std::make_unique<Arcbit::VulkanContext>();

    // Initialise the full Vulkan stack: instance → surface → physical device →
    // logical device → allocator → command pool.
    if (!context->Init(desc))
    {
        LOG_ERROR(Render, "Arcbit_CreateDevice: backend initialisation failed");
        return nullptr;
    }

    // Wrap the context in a RenderDevice and hand it to the caller.
    // The caller owns the pointer and must eventually pass it to Arcbit_DestroyDevice.
    return new Arcbit::RenderDevice(std::move(context));
}

ARCBIT_RENDER_API void Arcbit_DestroyDevice(Arcbit::RenderDevice* device)
{
    // delete invokes ~RenderDevice → VulkanContext::Shutdown → destroys all Vulkan objects.
    delete device;
}

} // extern "C"
