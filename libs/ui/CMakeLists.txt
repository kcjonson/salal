find_package(nlohmann_json CONFIG REQUIRED)
find_package(Stb REQUIRED)

# The Design Tokens loader and resolver (ui/theme/dtcg/), on nlohmann-json alone: the theme loads
# with it at run time, and salal_tokens() (cmake/SalalTokens.cmake) runs it at build time through
# salal_tokens_tool, so a consumer's build needs no Node. Built whenever ui is.
add_library(salal_ui_dtcg STATIC
    ui/theme/dtcg/Values.cpp
    ui/theme/dtcg/Document.cpp
    ui/theme/dtcg/Resolver.cpp
    ui/theme/dtcg/Codegen.cpp
)
target_compile_features(salal_ui_dtcg PUBLIC cxx_std_20)
target_include_directories(salal_ui_dtcg PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(salal_ui_dtcg PUBLIC nlohmann_json::nlohmann_json)
add_executable(salal_tokens_tool tools/TokensMain.cpp)
target_link_libraries(salal_tokens_tool PRIVATE salal_ui_dtcg)
set_target_properties(salal_ui_dtcg salal_tokens_tool PROPERTIES FOLDER salal)

# The icon build step (ui/iconbuild/) on msdfgen, nanosvg and stb_image_write, which salal_icons()
# (cmake/SalalIcons.cmake) runs through salal_icons_tool. Built where msdfgen is found, a host
# dependency a game that builds no icons needn't have.
find_package(msdfgen CONFIG QUIET)
if(msdfgen_FOUND)
    find_package(NanoSVG CONFIG REQUIRED)
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
    ui/Component.cpp
    ui/Context.cpp
    ui/Registry.cpp
    ui/Style.cpp
    ui/Transform.cpp
    ui/debug/LayoutLint.cpp
    ui/debug/TreeSnapshot.cpp
    ui/shapes/Circle.cpp
    ui/shapes/Line.cpp
    ui/shapes/Rectangle.cpp
    ui/shapes/Text.cpp
    ui/theme/Theme.cpp
    ui/theme/Variants.cpp
    ui/icons/IconAtlas.cpp
)
add_library(salal::ui ALIAS salal_ui)

# Headers are included as <ui/...> (docs/ui-plan.md, "ui headers under ui/"), so they can't collide with
# a game's own layout/ or components/ folders, or with worldsim's ui while both are linked.
target_compile_features(salal_ui PUBLIC cxx_std_20)
target_include_directories(salal_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${Stb_INCLUDE_DIR} # IconAtlas decodes its PNG with the renderer's stb_image
)

# nlohmann-json is public for Component::describe, whose header names it through json_fwd.hpp.
target_link_libraries(salal_ui
    PUBLIC
        salal::renderer
        salal::foundation
        nlohmann_json::nlohmann_json
    PRIVATE
        salal_ui_dtcg
)

set_target_properties(salal_ui PROPERTIES FOLDER salal)

# ui's default theme, tokens/ui.tokens.json: ui::styles, ui::tokens and the table every theme
# loads first.
salal_tokens(salal_ui tokens NAMESPACE ui HEADER ui/theme/Tokens.h)

salal_add_tests(ui)
if(TARGET salal_ui_tests)
    # The draw-order tests run Primitives on the null backend with the base shader.
    salal_base_shader(salal_ui_tests)
    # The tests drive the loader directly, and load every tokens file under the source tree.
    target_link_libraries(salal_ui_tests PRIVATE salal_ui_dtcg)
    target_compile_definitions(salal_ui_tests PRIVATE "SALAL_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    # A game's tokens over ui's, with a light and a dark mode, as salal_tokens() builds them for one.
    salal_tokens(salal_ui_tests tests/tokens NAMESPACE theme_test HEADER theme_test/Tokens.h)

    # A style name is checked by the compiler: the same file builds with ui::styles::pane and fails
    # with a misspelling, and the test passes only on that failure, naming it.
    add_executable(salal_ui_style_name tests/StyleName.cpp)
    target_link_libraries(salal_ui_style_name PRIVATE salal::ui)
    add_executable(salal_ui_style_typo EXCLUDE_FROM_ALL tests/StyleName.cpp)
    target_link_libraries(salal_ui_style_typo PRIVATE salal::ui)
    target_compile_definitions(salal_ui_style_typo PRIVATE SALAL_STYLE_TYPO)
    set_target_properties(salal_ui_style_name salal_ui_style_typo PROPERTIES FOLDER salal/tests)
    add_test(NAME salal_ui_style_name COMMAND salal_ui_style_name)
    add_test(
        NAME salal_ui_style_typo_fails_to_compile
        COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIR=${PROJECT_BINARY_DIR}" "-DCONFIG=$<CONFIG>" -DTARGET=salal_ui_style_typo -DEXPECT=pnae
                -P "${PROJECT_SOURCE_DIR}/cmake/SalalExpectCompileError.cmake"
    )
endif()
if(TARGET salal_ui_tests AND TARGET salal_ui_iconbuild)
    # The icon tests drive the build step directly, over the SVGs in tests/icons/, and decode what
    # it encodes with stb_image.
    target_link_libraries(salal_ui_tests PRIVATE salal_ui_iconbuild nlohmann_json::nlohmann_json)
    target_include_directories(salal_ui_tests PRIVATE ${Stb_INCLUDE_DIR})
    target_compile_definitions(salal_ui_tests PRIVATE "SALAL_UI_TEST_ICONS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/icons\"")
    # The step itself, as a game gets it, over the same folder.
    salal_icons(salal_ui_tests tests/icons NAMESPACE icon_test HEADER icon_test/Icons.h)
endif()
