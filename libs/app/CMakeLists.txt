add_library(app
    App.cpp
)

target_compile_features(app PUBLIC cxx_std_20)
target_include_directories(app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(app
    PUBLIC
        ui
)

salal_add_tests(app)
