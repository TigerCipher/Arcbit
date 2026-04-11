#pragma once

#include <arcbit/render/RenderTypes.h>

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <vector>
#include <optional>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Internal Vulkan resource types
// These never appear in engine or game headers.
// ---------------------------------------------------------------------------

struct VulkanBuffer
{
    VkBuffer      Buffer      = VK_NULL_HANDLE;
    VmaAllocation Allocation  = VK_NULL_HANDLE;
    u64           Size        = 0;
    BufferUsage   Usage       = BufferUsage::None;
    bool          HostVisible = false;
    void*         Mapped      = nullptr;   // non-null when HostVisible
};

struct VulkanTexture
{
    VkImage       Image      = VK_NULL_HANDLE;
    VkImageView   View       = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    VkFormat      Format     = VK_FORMAT_UNDEFINED;
    u32           Width      = 0;
    u32           Height     = 0;
    TextureUsage  Usage      = TextureUsage::None;
    bool          IsSwapchainImage = false;   // swapchain images aren't VMA-owned
};

struct VulkanSampler
{
    VkSampler Sampler = VK_NULL_HANDLE;
};

struct VulkanShader
{
    VkShaderModule Module = VK_NULL_HANDLE;
    ShaderStage    Stage  = ShaderStage::None;
};

struct VulkanPipeline
{
    VkPipeline            Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout      Layout   = VK_NULL_HANDLE;
};

struct VulkanSwapchain
{
    VkSwapchainKHR          Swapchain    = VK_NULL_HANDLE;
    VkSurfaceKHR            Surface      = VK_NULL_HANDLE;
    VkFormat                Format       = VK_FORMAT_UNDEFINED;
    VkExtent2D              Extent       = {};
    std::vector<VkImage>    Images;
    std::vector<VkImageView> ImageViews;

    // Sync — one set per frame in flight
    std::vector<VkSemaphore> ImageAvailable;   // signalled when image acquired
    std::vector<VkSemaphore> RenderFinished;   // signalled when rendering done
    std::vector<VkFence>     InFlight;         // CPU waits on this before reusing frame

    u32 CurrentImageIndex = 0;
    u32 CurrentFrame      = 0;
};

struct VulkanCommandList
{
    VkCommandBuffer Buffer = VK_NULL_HANDLE;
};

// ---------------------------------------------------------------------------
// HandlePool<T>
//
// Slot-map with generational indices. Matches the Handle<Tag> system.
// Index 0 is permanently reserved as "invalid".
//
// T must be default-constructible and copyable/movable.
// The caller is responsible for destroying GPU resources before calling Free.
// ---------------------------------------------------------------------------
template<typename T>
class HandlePool
{
public:
    HandlePool()
    {
        // Reserve slot 0 so index 0 always maps to "invalid"
        m_Slots.push_back({});
    }

    template<typename Tag>
    Handle<Tag> Allocate(T resource)
    {
        u32 index;

        if (!m_FreeList.empty())
        {
            index = m_FreeList.back();
            m_FreeList.pop_back();
        }
        else
        {
            index = static_cast<u32>(m_Slots.size());
            m_Slots.push_back({});
        }

        auto& slot      = m_Slots[index];
        slot.Resource   = std::move(resource);
        slot.Occupied   = true;
        // Generation was bumped on Free; keep it as-is here
        return Handle<Tag>::Make(index, slot.Generation);
    }

    template<typename Tag>
    T* Get(Handle<Tag> handle)
    {
        if (!handle.IsValid()) return nullptr;
        const u32 index = handle.Index();
        if (index >= m_Slots.size()) return nullptr;
        auto& slot = m_Slots[index];
        if (!slot.Occupied || slot.Generation != handle.Generation()) return nullptr;
        return &slot.Resource;
    }

    // Returns the resource and invalidates the slot.
    // Caller must destroy any GPU objects on the resource before calling this.
    template<typename Tag>
    std::optional<T> Free(Handle<Tag> handle)
    {
        if (!handle.IsValid()) return std::nullopt;
        const u32 index = handle.Index();
        if (index >= m_Slots.size()) return std::nullopt;
        auto& slot = m_Slots[index];
        if (!slot.Occupied || slot.Generation != handle.Generation()) return std::nullopt;

        T resource      = std::move(slot.Resource);
        slot.Resource   = {};
        slot.Occupied   = false;
        slot.Generation = (slot.Generation + 1) & Handle<Tag>::GenMask;
        m_FreeList.push_back(index);
        return resource;
    }

    u32 Count() const
    {
        return static_cast<u32>(m_Slots.size() - 1 - m_FreeList.size());
    }

private:
    struct Slot
    {
        T    Resource   = {};
        u32  Generation = 0;
        bool Occupied   = false;
    };

    std::vector<Slot> m_Slots;
    std::vector<u32>  m_FreeList;
};

// ---------------------------------------------------------------------------
// VulkanContext
//
// Owns all Vulkan state. Lives behind the RenderDevice Pimpl.
// ---------------------------------------------------------------------------
struct VulkanContext
{
    // Instance
    VkInstance               Instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;

    // Physical device
    VkPhysicalDevice                 PhysicalDevice      = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties       PhysicalDeviceProps = {};
    VkPhysicalDeviceFeatures         PhysicalDeviceFeats = {};

    // Queue families
    u32 GraphicsFamily = ~0u;
    u32 PresentFamily  = ~0u;

    // Logical device + queues
    VkDevice Device         = VK_NULL_HANDLE;
    VkQueue  GraphicsQueue  = VK_NULL_HANDLE;
    VkQueue  PresentQueue   = VK_NULL_HANDLE;

    // Memory allocator (VMA)
    VmaAllocator Allocator = VK_NULL_HANDLE;

    // Command pool (main thread — expand to per-thread pools later)
    VkCommandPool CommandPool = VK_NULL_HANDLE;

    // Resource pools
    HandlePool<VulkanBuffer>      Buffers;
    HandlePool<VulkanTexture>     Textures;
    HandlePool<VulkanSampler>     Samplers;
    HandlePool<VulkanShader>      Shaders;
    HandlePool<VulkanPipeline>    Pipelines;
    HandlePool<VulkanSwapchain>   Swapchains;
    HandlePool<VulkanCommandList> CommandLists;

    // Surface created during Init for device selection.
    // Handed off to the first swapchain via CreateSwapchain.
    VkSurfaceKHR m_InitSurface = VK_NULL_HANDLE;

    // --- Lifetime ---
    bool Init(const DeviceDesc& desc);
    void Shutdown();

private:
    bool CreateInstance(const char* appName, u32 appVersion, bool enableValidation);
    bool SelectPhysicalDevice(VkSurfaceKHR surface);
    bool CreateLogicalDevice();
    bool CreateAllocator();
    bool CreateCommandPool();

    void SetupDebugMessenger();
};

} // namespace Arcbit
