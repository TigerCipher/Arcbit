add_executable(arcbit-game
    src/main.cpp
)

target_link_libraries(arcbit-game PRIVATE
    arcbit-engine
    Vulkan::Vulkan
    SDL3::SDL3
    spdlog::spdlog
)

# ARCBIT_DEBUG enables Vulkan validation layers and other debug aids.
# Defined only in Debug builds; Release builds get none of this overhead.
target_compile_definitions(arcbit-game PRIVATE
    $<$<CONFIG:Debug>:ARCBIT_DEBUG>
)

# Windows: suppress the console window in Release builds.
if(WIN32)
    set_target_properties(arcbit-game PROPERTIES
        $<$<CONFIG:Release>:WIN32_EXECUTABLE TRUE>
    )
endif()
