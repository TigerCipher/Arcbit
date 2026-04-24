add_library(arcbit-vulkan SHARED
    src/VmaImpl.cpp
    src/VulkanContext.cpp
    src/VulkanDevice.cpp
)

target_compile_definitions(arcbit-vulkan PRIVATE
    ARCBIT_VULKAN_EXPORTS
)

target_include_directories(arcbit-vulkan PRIVATE
    src/
    ${Vulkan_INCLUDE_DIRS}   # picks up Include/vma/ and Include/vulkan/
)

target_link_libraries(arcbit-vulkan PRIVATE
    arcbit-engine            # render API headers + core headers
    arcbit-core              # logging, asserts
    Vulkan::Vulkan
    SDL3::SDL3
)
