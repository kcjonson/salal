find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_library(salal_app STATIC
    App.cpp
    application/CommandLine.cpp
    platform/GlfwPlatform.cpp
    platform/HeadlessPlatform.cpp
)
add_library(salal::app ALIAS salal_app)

target_compile_features(salal_app PUBLIC cxx_std_20)
target_include_directories(salal_app
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# GLFW and GL are the GLFW platform's, in platform/GlfwPlatform.cpp; no app header names them, so
# they stay private (salal_check_layering checks both).
target_link_libraries(salal_app
    PUBLIC
        salal::ui
        salal::renderer
        salal::foundation
    PRIVATE
        glfw
        OpenGL::GL
        GLEW::GLEW
)

set_target_properties(salal_app PROPERTIES FOLDER salal)

salal_add_tests(app)
