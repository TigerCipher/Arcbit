#include <arcbit/arcbit.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <spdlog/spdlog.h>

#include <vector>
#include <string>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Validation layer debug callback
// Only compiled in when ARCBIT_DEBUG is defined (Debug builds).
// ---------------------------------------------------------------------------
#ifdef ARCBIT_DEBUG

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       /*user_data*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        spdlog::error("[Vulkan] {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        spdlog::warn("[Vulkan] {}", data->pMessage);
    else
        spdlog::debug("[Vulkan] {}", data->pMessage);

    return VK_FALSE;
}

static VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance instance)
{
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = vk_debug_callback;

    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (fn)
        fn(instance, &info, nullptr, &messenger);
    return messenger;
}

static void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn && messenger != VK_NULL_HANDLE)
        fn(instance, messenger, nullptr);
}

#endif // ARCBIT_DEBUG

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int /*argc*/, char* /*argv*/[])
{
    spdlog::set_pattern("[%T.%e] [%^%l%$] %v");
#ifdef ARCBIT_DEBUG
    spdlog::set_level(spdlog::level::debug);
#endif
    spdlog::info("Arcbit starting");

    // -----------------------------------------------------------------------
    // SDL3 init + window
    // -----------------------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        spdlog::error("SDL_Init failed: {}", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Arcbit — build test",
        1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );
    if (!window)
    {
        spdlog::error("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }
    spdlog::info("Window created (1280x720)");

    // -----------------------------------------------------------------------
    // Vulkan instance
    // -----------------------------------------------------------------------

    // Extensions required by SDL3 to create a surface on this platform.
    uint32_t sdl_ext_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);

    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_ext_count);
    std::vector<const char*> layers;

#ifdef ARCBIT_DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    layers.push_back("VK_LAYER_KHRONOS_validation");
    spdlog::debug("Validation layers enabled");
#endif

    VkApplicationInfo app_info{};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = "Arcbit";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName        = "Arcbit Engine";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info{};
    instance_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo        = &app_info;
    instance_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();
    instance_info.enabledLayerCount       = static_cast<uint32_t>(layers.size());
    instance_info.ppEnabledLayerNames     = layers.data();

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&instance_info, nullptr, &instance);
    if (result != VK_SUCCESS)
    {
        spdlog::error("vkCreateInstance failed (VkResult = {})", static_cast<int>(result));
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    spdlog::info("Vulkan instance created (API 1.3)");

#ifdef ARCBIT_DEBUG
    VkDebugUtilsMessengerEXT debug_messenger = create_debug_messenger(instance);
#endif

    // -----------------------------------------------------------------------
    // Enumerate physical devices
    // -----------------------------------------------------------------------
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data());

    spdlog::info("Found {} physical device(s):", gpu_count);
    for (auto& gpu : gpus)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);

        const char* type = "Unknown";
        switch (props.deviceType)
        {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   type = "Discrete GPU";   break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: type = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    type = "Virtual GPU";    break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            type = "CPU";            break;
            default: break;
        }

        uint32_t major = VK_VERSION_MAJOR(props.apiVersion);
        uint32_t minor = VK_VERSION_MINOR(props.apiVersion);
        uint32_t patch = VK_VERSION_PATCH(props.apiVersion);
        spdlog::info("  [{}] {} — Vulkan {}.{}.{}", type, props.deviceName, major, minor, patch);
    }

    // -----------------------------------------------------------------------
    // Event loop
    // -----------------------------------------------------------------------
    spdlog::info("Entering event loop — press Escape or close the window to exit");

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE)
                        running = false;
                    break;
                default:
                    break;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Cleanup (reverse order of creation)
    // -----------------------------------------------------------------------
#ifdef ARCBIT_DEBUG
    destroy_debug_messenger(instance, debug_messenger);
#endif

    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();

    spdlog::info("Arcbit shutdown complete");
    return EXIT_SUCCESS;
}
