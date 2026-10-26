add_library(salal_app STATIC
    App.cpp
)
add_library(salal::app ALIAS salal_app)

target_compile_features(salal_app PUBLIC cxx_std_20)
target_include_directories(salal_app
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(salal_app
    PUBLIC
        salal::ui
)

set_target_properties(salal_app PROPERTIES FOLDER salal)

salal_add_tests(app)
