add_library(ui
    Ui.cpp
)

target_include_directories(ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ui
    PUBLIC
        renderer
        foundation
)

salal_add_tests(ui)
