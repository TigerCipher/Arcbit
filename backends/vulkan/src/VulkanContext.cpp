#include "VulkanContext.h"

#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

// Check if all requested layers are available.
bool CheckLayerSupport(const std::vector<const char*>& requested)
{
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    for (const char* name : requested)
    {
        bool found = false;
        for (const auto& layer : available)
        {
            if (strcmp(name, layer.layerName) == 0) { found = true; break; }
        }
        if (!found)
        {
            LOG_ERROR(Render, "Validation layer not available: {}", name);
            return false;
        }
    }
    return true;
}

// Score a physical device — higher is better.
// Returns -1 if the device doesn't meet minimum requirements.
i32 ScoreDevice(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    // Require Vulkan 1.3
    if (props.apiVersion < VK_API_VERSION_1_3) return -1;

    // Require swapchain extension
    u32 extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());

    bool hasSwapchain = false;
    for (const auto& ext : exts)
    {
        if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
        {
            hasSwapchain = true;
            break;
        }
    }
    if (!hasSwapchain) return -1;

    // Check required Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features feats13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceFeatures2 feats2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &feats13 };
    vkGetPhysicalDeviceFeatures2(device, &feats2);

    if (!feats13.dynamicRendering || !feats13.synchronization2) return -1;

    // Require at least one queue family that supports graphics + present
    u32 queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());

    bool hasGraphicsAndPresent = false;
    for (u32 i = 0; i < queueCount; ++i)
    {
        if (!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) { hasGraphicsAndPresent = true; break; }
    }
    if (!hasGraphicsAndPresent) return -1;

    // Score: discrete GPU wins over everything else
    i32 score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 1000;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 100;

    return score;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       /*user*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR(Render, "(validation) {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN(Render, "(validation) {}", data->pMessage);
    else
        LOG_DEBUG(Render, "(validation) {}", data->pMessage);

    return VK_FALSE;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// VulkanContext::Init
// ---------------------------------------------------------------------------
bool VulkanContext::Init(const DeviceDesc& desc)
{
    LOG_INFO(Render, "Initialising Vulkan backend");

    if (!CreateInstance(desc.AppName, desc.AppVersion, desc.EnableValidation))
        return false;

    if (desc.EnableValidation)
        SetupDebugMessenger();

    // Create a temporary surface so SelectPhysicalDevice can check present support.
    // We keep this surface and hand it off to the swapchain later.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(
            static_cast<SDL_Window*>(desc.NativeWindowHandle),
            Instance, nullptr, &surface))
    {
        LOG_ERROR(Render, "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }

    if (!SelectPhysicalDevice(surface))
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    if (!CreateLogicalDevice())
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    if (!CreateAllocator())
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    if (!CreateCommandPool())
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    // Store the surface — the swapchain will take ownership via CreateSwapchain.
    // We keep it here until then.
    // (Stored temporarily in the first swapchain slot via a raw field below)
    m_InitSurface = surface;

    LOG_INFO(Render, "Vulkan backend ready — {}",
        PhysicalDeviceProps.deviceName);
    return true;
}

// ---------------------------------------------------------------------------
// VulkanContext::Shutdown
// ---------------------------------------------------------------------------
void VulkanContext::Shutdown()
{
    vkDeviceWaitIdle(Device);

    if (CommandPool  != VK_NULL_HANDLE) vkDestroyCommandPool(Device, CommandPool, nullptr);
    if (Allocator    != VK_NULL_HANDLE) vmaDestroyAllocator(Allocator);
    if (Device       != VK_NULL_HANDLE) vkDestroyDevice(Device, nullptr);

    if (m_InitSurface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(Instance, m_InitSurface, nullptr);

    if (DebugMessenger != VK_NULL_HANDLE)
    {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(Instance, DebugMessenger, nullptr);
    }

    if (Instance != VK_NULL_HANDLE) vkDestroyInstance(Instance, nullptr);

    LOG_INFO(Render, "Vulkan backend shut down");
}

// ---------------------------------------------------------------------------
// CreateInstance
// ---------------------------------------------------------------------------
bool VulkanContext::CreateInstance(const char* appName, u32 appVersion, bool enableValidation)
{
    // Extensions required by SDL for surface creation
    u32 sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);
    std::vector<const char*> layers;

    if (enableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        if (!CheckLayerSupport(validationLayers))
        {
            LOG_WARN(Render, "Validation layers requested but not available — continuing without");
        }
        else
        {
            layers = validationLayers;
        }
    }

    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName   = appName;
    appInfo.applicationVersion = appVersion;
    appInfo.pEngineName        = "Arcbit";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount       = static_cast<u32>(layers.size());
    createInfo.ppEnabledLayerNames     = layers.data();

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &Instance);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vkCreateInstance failed ({})", static_cast<i32>(result));
        return false;
    }

    LOG_DEBUG(Render, "VkInstance created");
    return true;
}

// ---------------------------------------------------------------------------
// SetupDebugMessenger
// ---------------------------------------------------------------------------
void VulkanContext::SetupDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = DebugCallback;

    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));

    if (fn) fn(Instance, &info, nullptr, &DebugMessenger);
}

// ---------------------------------------------------------------------------
// SelectPhysicalDevice
// ---------------------------------------------------------------------------
bool VulkanContext::SelectPhysicalDevice(VkSurfaceKHR surface)
{
    u32 count = 0;
    vkEnumeratePhysicalDevices(Instance, &count, nullptr);
    if (count == 0)
    {
        LOG_ERROR(Render, "No Vulkan-capable GPUs found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(Instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    i32 bestScore = -1;

    for (auto& device : devices)
    {
        const i32 score = ScoreDevice(device, surface);
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        LOG_DEBUG(Render, "  GPU candidate: {} (score {})", props.deviceName, score);

        if (score > bestScore) { bestScore = score; best = device; }
    }

    if (best == VK_NULL_HANDLE)
    {
        LOG_ERROR(Render, "No suitable GPU found (requires Vulkan 1.3 + dynamic rendering + sync2)");
        return false;
    }

    PhysicalDevice = best;
    vkGetPhysicalDeviceProperties(PhysicalDevice, &PhysicalDeviceProps);
    vkGetPhysicalDeviceFeatures(PhysicalDevice, &PhysicalDeviceFeats);

    // Find queue families
    u32 queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queueCount, queues.data());

    for (u32 i = 0; i < queueCount; ++i)
    {
        if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && GraphicsFamily == ~0u)
            GraphicsFamily = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, i, surface, &presentSupport);
        if (presentSupport && PresentFamily == ~0u)
            PresentFamily = i;

        if (GraphicsFamily != ~0u && PresentFamily != ~0u) break;
    }

    LOG_INFO(Render, "Selected GPU: {} (graphics family {}, present family {})",
        PhysicalDeviceProps.deviceName, GraphicsFamily, PresentFamily);

    return true;
}

// ---------------------------------------------------------------------------
// CreateLogicalDevice
// ---------------------------------------------------------------------------
bool VulkanContext::CreateLogicalDevice()
{
    // Build unique queue create infos (graphics and present may be the same family)
    std::vector<u32> uniqueFamilies = { GraphicsFamily };
    if (PresentFamily != GraphicsFamily)
        uniqueFamilies.push_back(PresentFamily);

    const f32 queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (u32 family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &queuePriority;
        queueInfos.push_back(qi);
    }

    // Enable dynamic rendering and synchronization2 (Vulkan 1.3 core)
    VkPhysicalDeviceVulkan13Features feats13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    feats13.dynamicRendering = VK_TRUE;
    feats13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 feats2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &feats13 };

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    createInfo.pNext                   = &feats2;
    createInfo.queueCreateInfoCount    = static_cast<u32>(queueInfos.size());
    createInfo.pQueueCreateInfos       = queueInfos.data();
    createInfo.enabledExtensionCount   = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    const VkResult result = vkCreateDevice(PhysicalDevice, &createInfo, nullptr, &Device);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vkCreateDevice failed ({})", static_cast<i32>(result));
        return false;
    }

    vkGetDeviceQueue(Device, GraphicsFamily, 0, &GraphicsQueue);
    vkGetDeviceQueue(Device, PresentFamily,  0, &PresentQueue);

    LOG_DEBUG(Render, "VkDevice created");
    return true;
}

// ---------------------------------------------------------------------------
// CreateAllocator
// ---------------------------------------------------------------------------
bool VulkanContext::CreateAllocator()
{
    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.instance         = Instance;
    info.physicalDevice   = PhysicalDevice;
    info.device           = Device;

    const VkResult result = vmaCreateAllocator(&info, &Allocator);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vmaCreateAllocator failed ({})", static_cast<i32>(result));
        return false;
    }

    LOG_DEBUG(Render, "VMA allocator created");
    return true;
}

// ---------------------------------------------------------------------------
// CreateCommandPool
// ---------------------------------------------------------------------------
bool VulkanContext::CreateCommandPool()
{
    VkCommandPoolCreateInfo info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    info.queueFamilyIndex = GraphicsFamily;
    // RESET_COMMAND_BUFFER_BIT lets us reset individual command buffers
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    const VkResult result = vkCreateCommandPool(Device, &info, nullptr, &CommandPool);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vkCreateCommandPool failed ({})", static_cast<i32>(result));
        return false;
    }

    LOG_DEBUG(Render, "VkCommandPool created");
    return true;
}

} // namespace Arcbit
