find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_library(salal_app STATIC
    application/App.cpp
    application/CommandLine.cpp
    clipboard/Clipboard.cpp
    input/Input.cpp
    platform/Gamepads.cpp
    platform/GlfwInput.cpp
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

# GLFW and GL are the GLFW platform's, in platform/GlfwPlatform.cpp and GlfwInput.cpp; no app
# header names them, so they stay private (salal_check_layering checks both). winmm for
# timeBeginPeriod, which run()'s frame cap needs.
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

set_target_properties(salal_app PROPERTIES FOLDER salal)

salal_add_tests(app)
