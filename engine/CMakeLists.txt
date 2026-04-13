# arcbit-engine is a STATIC library that provides the engine's public API:
# graphics abstraction, render thread, window, input, and (in later phases) ECS, audio, etc.
# Game and tool targets link against this; they never see Vulkan or SDL headers directly.
add_library(arcbit-engine STATIC
    src/RenderThread.cpp
    src/Window.cpp
    src/InputManager.cpp
)

target_include_directories(arcbit-engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(arcbit-engine
    PUBLIC
        arcbit-core
    PRIVATE
        SDL3::SDL3  # used in Window.cpp and InputManager.cpp; not exposed through public headers
)

# Engine does NOT link Vulkan directly — its render interfaces are
# graphics-agnostic. Backends link Vulkan; the game links the backend DLL.
