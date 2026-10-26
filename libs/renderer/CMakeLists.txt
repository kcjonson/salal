find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(NanoSVG CONFIG REQUIRED)
find_package(Stb REQUIRED)

add_library(salal_renderer STATIC
    primitives/Primitives.cpp
    primitives/BatchRenderer.cpp
    primitives/Stroke.cpp
    font/FontRenderer.cpp
    stb_image.cpp
    metrics/MetricsCollector.cpp
    metrics/PerformanceMetrics.cpp
    metrics/GPUTimer.cpp
    vector/Tessellator.cpp
    vector/Bezier.cpp
    vector/SVGLoader.cpp
    vector/MeshBounds.cpp
    vector/tess/Predicates.cpp
    vector/tess/Mesh.cpp
    vector/tess/PriorityQueue.cpp
    vector/tess/Dict.cpp
    vector/tess/Sweep.cpp
    vector/tess/Triangulate.cpp
    backend/gl/GLTargets.cpp
    backend/gl/GLBackend.cpp
    backend/gl/Shader.cpp
    backend/gl/ShaderLoader.cpp
    backend/gl/ShaderPreprocessor.cpp
    resources/RenderTarget.cpp
    resources/TileTextureAtlas.cpp
    resources/TilePatternBaker.cpp
    resources/TileAtlasBuilder.cpp
    CoordinateSystem/CoordinateSystem.cpp
)
add_library(salal::renderer ALIAS salal_renderer)

target_compile_features(salal_renderer PUBLIC cxx_std_20)

target_include_directories(salal_renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${Stb_INCLUDE_DIR}
)

# GL stays behind the renderer's public headers (salal_check_layering walks their
# includes at configure time), so OpenGL and GLEW are PRIVATE; nothing in the library touches
# GLFW any more (the app feeds CoordinateSystem its window sizes). The GL test
# fixtures open their own context and link all three below.
target_link_libraries(salal_renderer
    PUBLIC
        salal::foundation
        glm::glm
    PRIVATE
        OpenGL::GL
        GLEW::GLEW
        NanoSVG::nanosvg             # SVGLoader.cpp and TilePatternBaker.cpp only
        nlohmann_json::nlohmann_json # FontRenderer.cpp parses the atlas JSON
)
set_target_properties(salal_renderer PROPERTIES FOLDER salal)

# Shader staging. Shader.cpp resolves "shaders/<file>" through findResource, which looks
# beside the executable first; consumers mirror ${CMAKE_BINARY_DIR}/shaders next to each
# exe. CMAKE_BINARY_DIR is the top-level build tree even when salal is a subproject.
file(GLOB_RECURSE SALAL_RENDERER_SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*")
set(SALAL_RENDERER_SHADERS_STAMP "${CMAKE_BINARY_DIR}/shaders/.salal_renderer_shaders_copied")
add_custom_command(
    OUTPUT ${SALAL_RENDERER_SHADERS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E touch ${SALAL_RENDERER_SHADERS_STAMP}
    DEPENDS ${SALAL_RENDERER_SHADER_SOURCES}
    COMMENT "Staging salal renderer shaders into ${CMAKE_BINARY_DIR}/shaders"
    VERBATIM
)
add_custom_target(salal_renderer_shaders ALL DEPENDS ${SALAL_RENDERER_SHADERS_STAMP})
add_dependencies(salal_renderer salal_renderer_shaders)
set_target_properties(salal_renderer_shaders PROPERTIES FOLDER salal)

salal_add_tests(renderer)
if(TARGET salal_renderer_tests)
    target_link_libraries(salal_renderer_tests PRIVATE OpenGL::GL GLEW::GLEW glfw)
    # The fixtures that draw through Primitives load the uber shaders from beside the exe,
    # copied when a shader changes (one stamp per configuration).
    set(SALAL_RENDERER_TESTS_SHADERS_STAMP "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/salal_renderer_tests_shaders.stamp")
    add_custom_command(
        OUTPUT ${SALAL_RENDERER_TESTS_SHADERS_STAMP}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:salal_renderer_tests>/shaders
        COMMAND ${CMAKE_COMMAND} -E touch ${SALAL_RENDERER_TESTS_SHADERS_STAMP}
        DEPENDS ${SALAL_RENDERER_SHADER_SOURCES}
        COMMENT "Mirroring shaders beside salal_renderer_tests"
        VERBATIM
    )
    add_custom_target(salal_renderer_tests_shaders DEPENDS ${SALAL_RENDERER_TESTS_SHADERS_STAMP})
    add_dependencies(salal_renderer_tests salal_renderer_tests_shaders)
    set_target_properties(salal_renderer_tests_shaders PROPERTIES FOLDER salal/tests)
endif()
