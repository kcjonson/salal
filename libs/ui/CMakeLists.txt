# The icon build step (ui/iconbuild/) on msdfgen, nanosvg and stb_image_write, which salal_icons()
# (cmake/SalalIcons.cmake) runs through salal_icons_tool. Built where msdfgen is found, a host
# dependency a game that builds no icons needn't have.
find_package(msdfgen CONFIG QUIET)
if(msdfgen_FOUND)
    find_package(NanoSVG CONFIG REQUIRED)
    find_package(Stb REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    add_library(salal_ui_iconbuild STATIC
        ui/iconbuild/SvgCheck.cpp
        ui/iconbuild/IconAtlasBuilder.cpp
    )
    target_compile_features(salal_ui_iconbuild PUBLIC cxx_std_20)
    target_include_directories(salal_ui_iconbuild
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        PRIVATE
            ${Stb_INCLUDE_DIR}
    )
    target_link_libraries(salal_ui_iconbuild PRIVATE msdfgen::msdfgen-core NanoSVG::nanosvg nlohmann_json::nlohmann_json)
    add_executable(salal_icons_tool tools/IconsMain.cpp)
    target_link_libraries(salal_icons_tool PRIVATE salal_ui_iconbuild)
    set_target_properties(salal_ui_iconbuild salal_icons_tool PROPERTIES FOLDER salal)
endif()

add_library(salal_ui STATIC
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
if(TARGET salal_ui_tests AND TARGET salal_ui_iconbuild)
    # The icon tests drive the build step directly, over the SVGs in tests/icons/, and decode what
    # it encodes with stb_image.
    target_link_libraries(salal_ui_tests PRIVATE salal_ui_iconbuild nlohmann_json::nlohmann_json)
    target_include_directories(salal_ui_tests PRIVATE ${Stb_INCLUDE_DIR})
    target_compile_definitions(salal_ui_tests PRIVATE "SALAL_UI_TEST_ICONS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/icons\"")
    # The step itself, as a game gets it, over the same folder.
    salal_icons(salal_ui_tests tests/icons NAMESPACE icon_test HEADER icon_test/Icons.h)
endif()
