add_library(arcbit-core STATIC
    src/Log.cpp
)

target_include_directories(arcbit-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(arcbit-core
    PUBLIC
        spdlog::spdlog
)

target_compile_definitions(arcbit-core
    PUBLIC
        $<$<CONFIG:Debug>:ARCBIT_DEBUG>
)
