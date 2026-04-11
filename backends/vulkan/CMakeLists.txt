add_library(arcbit_vulkan SHARED
    src/VmaImpl.cpp
    src/VulkanContext.cpp
    src/VulkanDevice.cpp
)

target_compile_definitions(arcbit_vulkan PRIVATE
    ARCBIT_VULKAN_EXPORTS
)

target_include_directories(arcbit_vulkan PRIVATE
    src/
    ${Vulkan_INCLUDE_DIRS}   # picks up Include/vma/ and Include/vulkan/
)

target_link_libraries(arcbit_vulkan PRIVATE
    arcbit-engine            # render API headers + core headers
    arcbit-core              # logging, asserts
    Vulkan::Vulkan
    SDL3::SDL3
)
