find_package(nlohmann_json CONFIG REQUIRED)

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
)
add_library(salal::ui ALIAS salal_ui)

# Headers are included as <ui/...> (docs/ui-plan.md, "ui headers under ui/"), so they can't collide with
# a game's own layout/ or components/ folders, or with worldsim's ui while both are linked.
target_compile_features(salal_ui PUBLIC cxx_std_20)
target_include_directories(salal_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# nlohmann-json is public for Component::describe, whose header names it through json_fwd.hpp.
target_link_libraries(salal_ui
    PUBLIC
        salal::renderer
        salal::foundation
        nlohmann_json::nlohmann_json
)

set_target_properties(salal_ui PROPERTIES FOLDER salal)

salal_add_tests(ui)
# The draw-order tests run Primitives on the null backend with the base shader.
if(TARGET salal_ui_tests)
    salal_base_shader(salal_ui_tests)
endif()
