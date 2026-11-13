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
    primitives/MaskImage.cpp
    primitives/BoundaryLoops.cpp
    primitives/Stroke.cpp
    font/FontRenderer.cpp
    stb_image.cpp
    PngEncoder.cpp
    metrics/MetricsCollector.cpp
    metrics/PerformanceMetrics.cpp
    metrics/GPUTimer.cpp
    metrics/GPUFrameTimer.cpp
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
    backend/NullBackend.cpp
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

# GL calls stay inside backend/gl/ and GL headers off the renderer's public headers
# (salal_check_layering walks their includes at configure time), so OpenGL and
# GLEW are PRIVATE; nothing in the library touches GLFW (the app feeds
# CoordinateSystem its window sizes). The GL test fixtures open their own context
# and link all three below.
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

# The base shader's build step, which salal_base_shader() (cmake/SalalBaseShader.cmake) runs
# for each consumer: a host tool that splices the consumer's hooks into shaders/base.vert and
# embeds the result as C++, so nothing in shaders/ is read at run time. The library is the
# tool's logic, which the tests run directly. Built whenever the renderer is.
add_library(salal_shader_assembly STATIC assembler/BaseShaderAssembler.cpp)
target_compile_features(salal_shader_assembly PUBLIC cxx_std_20)
target_include_directories(salal_shader_assembly PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
add_executable(salal_shader_assembler assembler/Main.cpp)
target_link_libraries(salal_shader_assembler PRIVATE salal_shader_assembly)
set_target_properties(salal_shader_assembly salal_shader_assembler PROPERTIES FOLDER salal)

# The vector art build step, which salal_art() (cmake/SalalArt.cmake) runs for each consumer: a host
# tool that checks a folder of SVG against what loadSVG reads and embeds it as C++. The library is
# the check and the generator, on a scanner of SVG markup that ui's icon check shares, and needs
# nothing else. Built whenever the renderer is.
add_library(salal_svg_build STATIC vector/SvgMarkup.cpp vector/ArtBuild.cpp)
target_compile_features(salal_svg_build PUBLIC cxx_std_20)
target_include_directories(salal_svg_build PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
add_executable(salal_art_tool tools/ArtMain.cpp)
target_link_libraries(salal_art_tool PRIVATE salal_svg_build)
set_target_properties(salal_svg_build salal_art_tool PROPERTIES FOLDER salal)

salal_add_tests(renderer)
if(TARGET salal_renderer_tests)
    target_link_libraries(salal_renderer_tests PRIVATE OpenGL::GL GLEW::GLEW glfw salal_shader_assembly salal_svg_build)
    # PngEncoder.test.cpp decodes what it encoded with stb_image.
    target_include_directories(salal_renderer_tests PRIVATE ${Stb_INCLUDE_DIR})
    # The base shader as a consumer with no hooks gets it, for every fixture; again with the test
    # vertex hook in tests/hooks/, which ShaderHooks.test.cpp draws with (its HEADER in a folder, as
    # a consumer may put it); and with the test fill hook in tests/fill_hooks/, which it and the
    # sandbox's fill_hook scene draw with.
    salal_base_shader(salal_renderer_tests)
    salal_base_shader(salal_renderer_tests HOOKS tests/hooks NAMESPACE offset_hook_shader HEADER generated/OffsetHookShader.h)
    salal_base_shader(salal_renderer_tests HOOKS tests/fill_hooks NAMESPACE stripes_shader HEADER generated/StripesShader.h)
    # A folder of art embedded as a consumer embeds it, which SVGLoader.test.cpp loads from memory, and
    # the tool run on a file it refuses, which has to fail naming the file, line and element.
    salal_art(salal_renderer_tests tests/art NAMESPACE test_art HEADER generated/TestArt.h)
    add_test(
        NAME salal_art_refuses_a_filter_naming_its_file_line_and_element
        COMMAND salal_art_tool ${CMAKE_CURRENT_SOURCE_DIR}/tests/art_refused refused Refused.h
                ${CMAKE_CURRENT_BINARY_DIR}/salal_art/refused/Refused.h ${CMAKE_CURRENT_BINARY_DIR}/salal_art/refused/Refused.cpp
    )
    # The message is only printed when the tool fails, so matching it is the failure too.
    set_tests_properties(
        salal_art_refuses_a_filter_naming_its_file_line_and_element PROPERTIES PASS_REGULAR_EXPRESSION "glow\\.svg:4: <filter> "
    )
endif()
