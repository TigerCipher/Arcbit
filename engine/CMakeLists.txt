find_package(nlohmann_json CONFIG REQUIRED)
find_package(Stb REQUIRED) # header-only; exposes ${Stb_INCLUDE_DIR}
# miniaudio is header-only; vcpkg adds its include path automatically — no find_package needed.

# arcbit-engine is a STATIC library that provides the engine's public API:
# graphics abstraction, render thread, window, input, and (in later phases) ECS, audio, etc.
# Game and tool targets link against this; they never see Vulkan or SDL headers directly.
add_library(arcbit-engine STATIC
    src/Application.cpp
    src/Camera2D.cpp
    src/RenderThread.cpp
    src/Window.cpp
    src/InputManager.cpp
    src/Settings.cpp
    src/TextureManager.cpp
    src/SpriteSheet.cpp
    src/World.cpp
    src/Systems.cpp
    src/Scene.cpp
    src/AnimatorStateMachine.cpp
    src/AudioManager.cpp
)

target_include_directories(arcbit-engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(arcbit-engine
    PUBLIC
        arcbit-core
    PRIVATE
        SDL3::SDL3                    # used in Window.cpp and InputManager.cpp; not exposed through public headers
        nlohmann_json::nlohmann_json  # used in Settings.cpp and SpriteSheet.cpp
)

# stb is header-only; vcpkg exposes it as an include directory, not a CMake target.
# Only TextureManager.cpp needs it (STB_IMAGE_IMPLEMENTATION defined there).
target_include_directories(arcbit-engine PRIVATE ${Stb_INCLUDE_DIR})

# Engine does NOT link Vulkan directly — its render interfaces are
# graphics-agnostic. Backends link Vulkan; the game links the backend DLL.
