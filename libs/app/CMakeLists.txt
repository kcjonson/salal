find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
if(SALAL_WITH_DEV_SERVER)
    find_package(httplib CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

add_library(salal_app STATIC
    application/App.cpp
    application/CommandLine.cpp
    platform/GlfwPlatform.cpp
    platform/HeadlessPlatform.cpp
    scene/SceneStack.cpp
)
add_library(salal::app ALIAS salal_app)

target_compile_features(salal_app PUBLIC cxx_std_20)
target_include_directories(salal_app
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# GLFW and GL are the GLFW platform's, in platform/GlfwPlatform.cpp; no app header names them, so
# they stay private (salal_check_layering checks both). winmm for timeBeginPeriod, which run()'s
# frame cap needs.
target_link_libraries(salal_app
    PUBLIC
        salal::renderer
        salal::foundation
    PRIVATE
        glfw
        OpenGL::GL
        GLEW::GLEW
        $<$<PLATFORM_ID:Windows>:winmm>
)

# The dev server, only in the configurations SALAL_DEV_SERVER_GENEX names. SALAL_DEV_SERVER is
# PUBLIC so a game can #if on it; elsewhere DevServer.h declares the same API as inline no-ops, none
# of these sources is compiled, and nothing links cpp-httplib (salal_check_layering checks the link
# carries the same condition as the define).
if(SALAL_WITH_DEV_SERVER)
    target_compile_definitions(salal_app PUBLIC $<${SALAL_DEV_SERVER_GENEX}:SALAL_DEV_SERVER=1>)
    target_sources(salal_app PRIVATE
        $<${SALAL_DEV_SERVER_GENEX}:DevServer/DevRegistry.cpp>
        $<${SALAL_DEV_SERVER_GENEX}:DevServer/DevServer.cpp>
        $<${SALAL_DEV_SERVER_GENEX}:DevServer/DevServerHost.cpp>
        $<${SALAL_DEV_SERVER_GENEX}:DevServer/InputParser.cpp>
    )
    target_link_libraries(salal_app PRIVATE
        $<${SALAL_DEV_SERVER_GENEX}:httplib::httplib>
        $<${SALAL_DEV_SERVER_GENEX}:nlohmann_json::nlohmann_json>
    )
endif()

set_target_properties(salal_app PROPERTIES FOLDER salal)

salal_add_tests(app)
# The route tests talk to the server over loopback with cpp-httplib's client and read its JSON;
# they compile to nothing without the server.
if(TARGET salal_app_tests AND SALAL_WITH_DEV_SERVER)
    target_link_libraries(salal_app_tests PRIVATE
        $<${SALAL_DEV_SERVER_GENEX}:httplib::httplib>
        $<${SALAL_DEV_SERVER_GENEX}:nlohmann_json::nlohmann_json>
    )
endif()
