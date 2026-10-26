add_library(renderer
    Renderer.cpp
)

target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(renderer
    PUBLIC
        foundation
)

salal_add_tests(renderer)
