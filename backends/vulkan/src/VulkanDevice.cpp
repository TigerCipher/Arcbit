#include <arcbit/render/RenderDevice.h>
#include "VulkanContext.h"

#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// RenderDevice — construction / destruction
// ---------------------------------------------------------------------------
RenderDevice::RenderDevice(std::unique_ptr<VulkanContext> context)
    : m_Context(std::move(context))
{}

RenderDevice::~RenderDevice()
{
    if (m_Context)
        m_Context->Shutdown();
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------
BufferHandle RenderDevice::CreateBuffer(const BufferDesc& desc)
{
    ARCBIT_ASSERT(desc.Size > 0, "Buffer size must be > 0");

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size        = desc.Size;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (HasFlag(desc.Usage, BufferUsage::Vertex))   bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Index))    bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Uniform))  bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Storage))  bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Transfer)) bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                                                                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = desc.HostVisible
        ? VMA_MEMORY_USAGE_CPU_TO_GPU   // host-visible, write-combined
        : VMA_MEMORY_USAGE_GPU_ONLY;    // device-local, fastest

    if (desc.HostVisible)
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VulkanBuffer vkBuffer{};
    vkBuffer.Size       = desc.Size;
    vkBuffer.Usage      = desc.Usage;
    vkBuffer.HostVisible = desc.HostVisible;

    VmaAllocationInfo allocResult{};
    const VkResult result = vmaCreateBuffer(
        m_Context->Allocator,
        &bufferInfo, &allocInfo,
        &vkBuffer.Buffer, &vkBuffer.Allocation,
        &allocResult);

    ARCBIT_VERIFY(result == VK_SUCCESS, "vmaCreateBuffer failed");

    if (desc.HostVisible)
        vkBuffer.Mapped = allocResult.pMappedData;

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

    return m_Context->Buffers.Allocate<BufferTag>(std::move(vkBuffer));
}

void RenderDevice::UpdateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset)
{
    VulkanBuffer* buf = m_Context->Buffers.Get(handle);
    ARCBIT_ASSERT(buf != nullptr, "UpdateBuffer: invalid handle");
    ARCBIT_ASSERT(offset + size <= buf->Size, "UpdateBuffer: write out of bounds");

    if (buf->HostVisible)
    {
        // Direct mapped write — no staging needed
        memcpy(static_cast<u8*>(buf->Mapped) + offset, data, size);
    }
    else
    {
        // Stage through a temporary host-visible buffer, then copy on GPU
        BufferDesc stagingDesc{};
        stagingDesc.Size        = size;
        stagingDesc.Usage       = BufferUsage::Transfer;
        stagingDesc.HostVisible = true;
        stagingDesc.DebugName   = "staging";

        BufferHandle stagingHandle = CreateBuffer(stagingDesc);
        VulkanBuffer* staging = m_Context->Buffers.Get(stagingHandle);

        memcpy(staging->Mapped, data, size);

        // Issue an immediate copy command
        VkCommandBufferAllocateInfo cmdAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdAlloc.commandPool        = m_Context->CommandPool;
        cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(m_Context->Device, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy region{ 0, offset, size };
        vkCmdCopyBuffer(cmd, staging->Buffer, buf->Buffer, 1, &region);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        vkQueueSubmit(m_Context->GraphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GraphicsQueue);

        vkFreeCommandBuffers(m_Context->Device, m_Context->CommandPool, 1, &cmd);

        DestroyBuffer(stagingHandle);
    }
}

void RenderDevice::DestroyBuffer(BufferHandle handle)
{
    auto resource = m_Context->Buffers.Free(handle);
    if (!resource) return;

    if (resource->Buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_Context->Allocator, resource->Buffer, resource->Allocation);
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
ShaderHandle RenderDevice::CreateShader(const ShaderDesc& desc)
{
    ARCBIT_ASSERT(desc.Code != nullptr && desc.CodeSize > 0, "Shader code must not be empty");
    ARCBIT_ASSERT(desc.CodeSize % 4 == 0, "SPIR-V bytecode must be 4-byte aligned");

    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = desc.CodeSize;
    info.pCode    = reinterpret_cast<const u32*>(desc.Code);

    VulkanShader vkShader{};
    vkShader.Stage = desc.Stage;

    const VkResult result = vkCreateShaderModule(m_Context->Device, &info, nullptr, &vkShader.Module);
    ARCBIT_VERIFY(result == VK_SUCCESS, "vkCreateShaderModule failed");

    return m_Context->Shaders.Allocate<ShaderTag>(std::move(vkShader));
}

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

void RenderDevice::WaitIdle()
{
    if (m_Context && m_Context->Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_Context->Device);
}

} // namespace Arcbit

// ---------------------------------------------------------------------------
// Factory — must be outside namespace Arcbit (C linkage + namespace don't mix)
// ---------------------------------------------------------------------------
extern "C"
{

ARCBIT_RENDER_API Arcbit::RenderDevice* Arcbit_CreateDevice(const Arcbit::DeviceDesc& desc)
{
    auto context = std::make_unique<Arcbit::VulkanContext>();
    if (!context->Init(desc))
    {
        LOG_ERROR(Render, "Arcbit_CreateDevice: backend initialisation failed");
        return nullptr;
    }
    return new Arcbit::RenderDevice(std::move(context));
}

ARCBIT_RENDER_API void Arcbit_DestroyDevice(Arcbit::RenderDevice* device)
{
    delete device;
}

} // extern "C"
