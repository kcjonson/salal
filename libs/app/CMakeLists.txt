find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(SALAL_WITH_DEV_SERVER)
    find_package(httplib CONFIG QUIET)
    if(NOT httplib_FOUND)
        message(FATAL_ERROR
            "salal's dev server needs cpp-httplib: add cpp-httplib to your vcpkg manifest, or configure with "
            "-DSALAL_WITH_DEV_SERVER=OFF for a build with no dev server in any configuration (docs/app-api.md, \"Dev server\").")
    endif()
endif()

add_library(salal_app STATIC
    application/App.cpp
    application/CommandLine.cpp
    clipboard/Clipboard.cpp
    detail/SafeFile.cpp
    input/ControlNames.cpp
    input/Input.cpp
    platform/EventRelay.cpp
    platform/Gamepads.cpp
    platform/GlfwInput.cpp
    platform/GlfwPlatform.cpp
    platform/HeadlessPlatform.cpp
    saves/SaveStore.cpp
    scene/SceneStack.cpp
    settings/Bindings.cpp
    settings/Settings.cpp
    storage/UserDirectories.cpp
)
add_library(salal::app ALIAS salal_app)

target_compile_features(salal_app PUBLIC cxx_std_20)
target_include_directories(salal_app
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# GLFW and GL are the GLFW platform's, in platform/GlfwPlatform.cpp and GlfwInput.cpp; no app
# header names them, so they stay private (salal_check_layering checks both). nlohmann-json is in
# the settings' and saves' headers, so it's public, in every configuration. winmm for
# timeBeginPeriod, which run()'s frame cap needs; shell32 and ole32 for the known folders the
# per-user directories fall back to.
target_link_libraries(salal_app
    PUBLIC
        salal::renderer
        salal::foundation
        nlohmann_json::nlohmann_json
    PRIVATE
        glfw
        OpenGL::GL
        GLEW::GLEW
        $<$<PLATFORM_ID:Windows>:winmm>
        $<$<PLATFORM_ID:Windows>:shell32>
        $<$<PLATFORM_ID:Windows>:ole32>
)

# The dev server, only in the configurations SALAL_DEV_SERVER_GENEX names. SALAL_DEV_SERVER is
# PUBLIC so a game can #if on it; elsewhere DevServer.h declares the same API as inline no-ops, none
# of these sources is compiled, and nothing links cpp-httplib. salal_check_layering is a tripwire on
# the link alone: it fails an httplib link on salal_app that isn't wrapped in the genex, not one
# reached through some other target, and it doesn't check the define or the sources.
if(SALAL_WITH_DEV_SERVER)
    target_compile_definitions(salal_app PUBLIC $<${SALAL_DEV_SERVER_GENEX}:SALAL_DEV_SERVER=1>)
    target_sources(salal_app PRIVATE
        $<${SALAL_DEV_SERVER_GENEX}:DevServer/DevRegistry.cpp>
        $<${SALAL_DEV_SERVER_GENEX}:DevServer/DevServer.cpp>
        $<${SALAL_DEV_SERVER_GENEX}:DevServer/DevServerHost.cpp>
        $<${SALAL_DEV_SERVER_GENEX}:DevServer/InputParser.cpp>
    )
    target_link_libraries(salal_app PRIVATE $<${SALAL_DEV_SERVER_GENEX}:httplib::httplib>)
endif()

set_target_properties(salal_app PROPERTIES FOLDER salal)

salal_add_tests(app)
# The route tests talk to the server over loopback with cpp-httplib's client; they compile to
# nothing without the server.
if(TARGET salal_app_tests AND SALAL_WITH_DEV_SERVER)
    target_link_libraries(salal_app_tests PRIVATE $<${SALAL_DEV_SERVER_GENEX}:httplib::httplib>)
endif()
