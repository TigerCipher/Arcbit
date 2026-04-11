add_executable(arcbit-game
    src/main.cpp
)

target_link_libraries(arcbit-game PRIVATE
    arcbit-engine      # pulls in arcbit-core transitively
    arcbit_vulkan      # renderer backend DLL (implicit link)
    SDL3::SDL3
)

# ARCBIT_DEBUG propagates from arcbit-core as a PUBLIC define.

# Windows: suppress the console window in Release builds.
if(WIN32)
    set_target_properties(arcbit-game PROPERTIES
        $<$<CONFIG:Release>:WIN32_EXECUTABLE TRUE>
    )
endif()
