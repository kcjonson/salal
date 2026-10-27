# salal_base_shader(<target> [HOOKS <dir>] [NAMESPACE <name>] [HEADER <file>])
#
# The renderer's base shader for <target>, assembled at build time (docs/shader-hooks.md):
# base.vert and base.frag with their includes resolved and every hook in <dir> (<name>.vert.glsl)
# spliced into the vertex stage, both stages checked and linked by glslangValidator, and compiled
# into <target> as a generated source. Its header, HEADER (default BaseShader.h) on <target>'s
# include path, declares <name>::source() (default base_shader), what Primitives::init takes, and
# a HookUniformBlock constant for each hook's uniform block. With no HOOKS the hook chain is empty.
# A shader error, a hook that breaks the contract, or anything in <dir> that is not a hook fails
# the build. <target> links salal::renderer, whose backend/Backend.h the header includes.
#
# Needs glslangValidator: the vcpkg port glslang with its tools feature, as a host dependency.
function(salal_base_shader target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "HOOKS;NAMESPACE;HEADER" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "salal_base_shader(${target}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "salal_base_shader: ${target} is not a target")
    endif()
    if(NOT arg_NAMESPACE)
        set(arg_NAMESPACE base_shader)
    endif()
    if(NOT arg_HEADER)
        set(arg_HEADER BaseShader.h)
    endif()

    find_program(SALAL_GLSLANG_VALIDATOR glslangValidator DOC "glslangValidator, which checks the assembled base shader")
    if(NOT SALAL_GLSLANG_VALIDATOR)
        message(FATAL_ERROR
            "salal_base_shader: glslangValidator not found. Add the glslang port with its tools feature to the "
            "vcpkg manifest as a host dependency, or set SALAL_GLSLANG_VALIDATOR.")
    endif()

    set(cmake_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    get_filename_component(shaders "${cmake_dir}/../libs/renderer/shaders" ABSOLUTE)
    file(GLOB_RECURSE base_sources CONFIGURE_DEPENDS "${shaders}/*")

    # Every entry, folders too, so the assembler can refuse what is not a hook.
    set(hooks "")
    if(DEFINED arg_HOOKS)
        get_filename_component(hooks_dir "${arg_HOOKS}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${hooks_dir}")
            message(FATAL_ERROR "salal_base_shader(${target}): HOOKS ${arg_HOOKS} is not a folder")
        endif()
        file(GLOB hooks LIST_DIRECTORIES true CONFIGURE_DEPENDS "${hooks_dir}/*")
    endif()

    get_filename_component(stem "${arg_HEADER}" NAME_WLE)
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/salal_base_shader/${target}/${arg_NAMESPACE}")
    set(header "${dir}/include/${arg_HEADER}")
    set(source "${dir}/${stem}.cpp")
    add_custom_command(
        OUTPUT "${header}" "${source}"
        COMMAND salal_shader_assembler assemble "${shaders}" "${dir}" ${hooks}
        COMMAND "${CMAKE_COMMAND}" "-DGLSLANG=${SALAL_GLSLANG_VALIDATOR}" "-DDIR=${dir}" -P "${cmake_dir}/SalalGlslang.cmake"
        COMMAND salal_shader_assembler embed "${dir}" "${arg_NAMESPACE}" "${header}" "${source}" ${hooks}
        DEPENDS salal_shader_assembler ${base_sources} ${hooks} "${cmake_dir}/SalalGlslang.cmake"
        COMMENT "Assembling the base shader for ${target} (${arg_NAMESPACE})"
        VERBATIM
    )
    target_sources(${target} PRIVATE "${source}")
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${dir}/include>")
endfunction()
