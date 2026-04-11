add_library(arcbit-core SHARED
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
    PRIVATE
        ARCBIT_CORE_EXPORTS
    PUBLIC
        $<$<CONFIG:Debug>:ARCBIT_DEBUG>
)
