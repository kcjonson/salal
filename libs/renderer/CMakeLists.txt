add_library(salal_renderer
    Renderer.cpp
)
add_library(salal::renderer ALIAS salal_renderer)

target_compile_features(salal_renderer PUBLIC cxx_std_20)
target_include_directories(salal_renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(salal_renderer
    PUBLIC
        salal::foundation
)

set_target_properties(salal_renderer PROPERTIES FOLDER salal)

salal_add_tests(renderer)
