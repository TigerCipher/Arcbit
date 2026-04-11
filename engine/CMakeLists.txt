# arcbit-engine is an INTERFACE library for now — it provides include paths
# and will accumulate headers as we build the abstract render API, ECS, etc.
# It will graduate to a STATIC library once it has source files of its own.
add_library(arcbit-engine INTERFACE)

target_include_directories(arcbit-engine
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(arcbit-engine
    INTERFACE
        arcbit-core
)

# Engine does NOT link Vulkan directly — its render interfaces are
# graphics-agnostic. Backends link Vulkan; the game links the backend DLL.
