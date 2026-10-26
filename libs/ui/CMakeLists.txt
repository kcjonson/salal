add_library(salal_ui
    Ui.cpp
)
add_library(salal::ui ALIAS salal_ui)

target_compile_features(salal_ui PUBLIC cxx_std_20)
target_include_directories(salal_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(salal_ui
    PUBLIC
        salal::renderer
        salal::foundation
)

set_target_properties(salal_ui PROPERTIES FOLDER salal)

salal_add_tests(ui)
