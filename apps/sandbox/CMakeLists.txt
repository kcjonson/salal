find_package(nlohmann_json CONFIG REQUIRED)

add_executable(salal_sandbox
    main.cpp
    Picker.cpp
    scenes/ArcScene.cpp
    scenes/BlendScene.cpp
    scenes/InputTestScene.cpp
    scenes/PolygonScene.cpp
    scenes/PolylineScene.cpp
    scenes/SdfMinimalScene.cpp
    scenes/UiClipScene.cpp
    scenes/UiControlsScene.cpp
    scenes/UiDisplayScene.cpp
    scenes/UiFocusScene.cpp
    scenes/UiInputScene.cpp
    scenes/UiMotionScene.cpp
    scenes/UiOverlayComponentsScene.cpp
    scenes/UiOverlaysScene.cpp
    scenes/UiLayoutScene.cpp
    scenes/UiOrderScene.cpp
    scenes/UiScrollScene.cpp
    scenes/UiTextScene.cpp
    scenes/UiIconsScene.cpp
    scenes/UiThemeScene.cpp
)

target_include_directories(salal_sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# nlohmann_json for the picker's and input_test's state JSON.
target_link_libraries(salal_sandbox
    PRIVATE
        salal::app
        nlohmann_json::nlohmann_json
)

# The base shader with no hooks, as a game with none gets it.
salal_base_shader(salal_sandbox)

# The sandbox's theme, ui's tokens with a light mode over them, which ui_theme switches between.
salal_tokens(salal_sandbox tokens NAMESPACE sandbox_theme HEADER sandbox_theme/Tokens.h)

# The sandbox's icons, which ui_icons draws.
salal_icons(salal_sandbox icons NAMESPACE sandbox_icons HEADER sandbox_icons/Icons.h)

set_target_properties(salal_sandbox PROPERTIES OUTPUT_NAME sandbox FOLDER salal)

# The JetBrains Mono atlas the picker and the scenes draw text with, and the font's licence, copied
# beside the exe on every build, where AppConfig::fontAtlases' paths resolve. Salal's libraries ship
# no font; this is the sandbox's own.
set(sandbox_fonts
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/JetBrainsMono-SDF.png
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/JetBrainsMono-SDF.json
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/OFL.txt
)
add_custom_target(salal_sandbox_fonts
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:salal_sandbox>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${sandbox_fonts} $<TARGET_FILE_DIR:salal_sandbox>/fonts
    COMMENT "Staging the sandbox's font atlas"
    VERBATIM
)
set_target_properties(salal_sandbox_fonts PROPERTIES FOLDER salal)
add_dependencies(salal_sandbox salal_sandbox_fonts)
