add_library(app
    App.cpp
)

target_include_directories(app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(app
    PUBLIC
        ui
)

salal_add_tests(app)
