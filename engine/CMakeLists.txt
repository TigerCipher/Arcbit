# arcbit-engine is a STATIC library that provides the engine's public API:
# graphics abstraction, render thread, and (in later phases) ECS, audio, etc.
# Game and tool targets link against this; they never see Vulkan headers.
add_library(arcbit-engine STATIC
    src/RenderThread.cpp
)

target_include_directories(arcbit-engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(arcbit-engine
    PUBLIC
        arcbit-core
)

# Engine does NOT link Vulkan directly — its render interfaces are
# graphics-agnostic. Backends link Vulkan; the game links the backend DLL.
