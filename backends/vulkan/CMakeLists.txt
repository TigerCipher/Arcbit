# arcbit_vulkan.dll — Vulkan renderer backend.
# Not yet wired into the root build (add_subdirectory is commented out there).
# This file is a placeholder; implementation begins in Phase 1.

add_library(arcbit_vulkan SHARED)

target_sources(arcbit_vulkan PRIVATE
    # src/VulkanDevice.cpp
    # src/VulkanSwapchain.cpp
    # src/VulkanCommandList.cpp
)

target_link_libraries(arcbit_vulkan PRIVATE
    arcbit-engine
    Vulkan::Vulkan
)

target_include_directories(arcbit_vulkan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
