# salal_base_shader(<target> [HOOKS <dir>] [NAMESPACE <name>] [HEADER <file>])
#
# The renderer's base shader for <target>, assembled at build time (docs/shader-hooks.md):
# base.vert and base.frag with their includes resolved and every hook in <dir> (<name>.vert.glsl)
# spliced into the vertex stage, both stages checked and linked by glslangValidator, and compiled
# into <target> as a generated source. Its header, HEADER (default BaseShader.h, a relative path,
# folders allowed) on <target>'s include path, declares <name>::source() (default base_shader), what
# Primitives::init takes, and HookUniformBlock and HookUniformMember constants for each hook's
# uniform block. With no HOOKS the hook chain is empty. A shader error or warning, a hook that
# breaks the contract, or anything in <dir> that is not a hook fails the build; entries whose names
# start with a dot are left out. <target> links salal::renderer, whose backend/Backend.h the header
# includes. The step is the target <target>_<name>, which <target> waits for.
#
# Needs glslangValidator: the vcpkg port glslang with its tools feature, as a host dependency.
function(salal_base_shader target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "HOOKS;NAMESPACE;HEADER" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "salal_base_shader(${target}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(arg_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "salal_base_shader(${target}): ${arg_KEYWORDS_MISSING_VALUES} given without a value")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "salal_base_shader: ${target} is not a target")
    endif()
    if(NOT DEFINED arg_NAMESPACE)
        set(arg_NAMESPACE base_shader)
    endif()
    if(NOT arg_NAMESPACE MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "salal_base_shader(${target}): NAMESPACE ${arg_NAMESPACE} is not a plain C++ identifier")
    endif()
    if(NOT DEFINED arg_HEADER)
        set(arg_HEADER BaseShader.h)
    endif()
    if(IS_ABSOLUTE "${arg_HEADER}" OR arg_HEADER MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "salal_base_shader(${target}): HEADER ${arg_HEADER} must be a path inside the include folder")
    endif()

    # One header and one namespace per call: two calls on a target that share either would put two
    # headers under one name on its include path, or two steps in one folder.
    get_target_property(taken ${target} SALAL_BASE_SHADER_HEADERS)
    if(NOT taken)
        set(taken "")
    endif()
    if("header:${arg_HEADER}" IN_LIST taken)
        message(FATAL_ERROR "salal_base_shader(${target}): another call on ${target} already makes HEADER ${arg_HEADER}; give this one its own")
    endif()
    if("namespace:${arg_NAMESPACE}" IN_LIST taken)
        message(FATAL_ERROR "salal_base_shader(${target}): another call on ${target} already uses NAMESPACE ${arg_NAMESPACE}; give this one its own")
    endif()
    set_property(TARGET ${target} APPEND PROPERTY SALAL_BASE_SHADER_HEADERS "header:${arg_HEADER}" "namespace:${arg_NAMESPACE}")

    find_program(SALAL_GLSLANG_VALIDATOR glslangValidator DOC "glslangValidator, which checks the assembled base shader")
    if(NOT SALAL_GLSLANG_VALIDATOR)
        message(FATAL_ERROR
            "salal_base_shader: glslangValidator not found. Add the glslang port with its tools feature to the "
            "vcpkg manifest as a host dependency, or set SALAL_GLSLANG_VALIDATOR.")
    endif()

    set(cmake_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    get_filename_component(shaders "${cmake_dir}/../libs/renderer/shaders" ABSOLUTE)
    file(GLOB_RECURSE base_sources CONFIGURE_DEPENDS "${shaders}/*")

    # The tool lists the folder itself; the entries here are for DEPENDS (folders too, so the tool
    # can refuse them) and for reconfiguring when one comes or goes. Dot entries stay out.
    set(hooks_dir "")
    set(hooks "")
    if(DEFINED arg_HOOKS)
        get_filename_component(hooks_dir "${arg_HOOKS}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${hooks_dir}")
            message(FATAL_ERROR "salal_base_shader(${target}): HOOKS ${arg_HOOKS} is not a folder")
        endif()
        file(GLOB hooks LIST_DIRECTORIES true CONFIGURE_DEPENDS "${hooks_dir}/*")
        list(FILTER hooks EXCLUDE REGEX "/\\.[^/]*$")
    endif()

    get_filename_component(stem "${arg_HEADER}" NAME_WLE)
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/salal_base_shader/${target}/${arg_NAMESPACE}")
    set(header "${dir}/include/${arg_HEADER}")
    set(source "${dir}/${stem}.cpp")
    add_custom_command(
        OUTPUT "${header}" "${source}"
        COMMAND salal_shader_assembler assemble "${shaders}" "${dir}" ${hooks_dir}
        COMMAND "${CMAKE_COMMAND}" "-DGLSLANG=${SALAL_GLSLANG_VALIDATOR}" "-DDIR=${dir}" -P "${cmake_dir}/SalalGlslang.cmake"
        COMMAND salal_shader_assembler embed "${dir}" "${arg_NAMESPACE}" "${header}" "${source}" ${hooks_dir}
        DEPENDS salal_shader_assembler ${base_sources} ${hooks} "${SALAL_GLSLANG_VALIDATOR}" "${cmake_dir}/SalalGlslang.cmake"
        COMMENT "Assembling the base shader for ${target} (${arg_NAMESPACE})"
        VERBATIM
    )
    # Its own target, the command's one owner under every generator, and one that builds alone
    # (CI's clang-tidy needs the header and nothing else).
    add_custom_target(${target}_${arg_NAMESPACE} DEPENDS "${header}" "${source}")
    set_target_properties(${target}_${arg_NAMESPACE} PROPERTIES FOLDER salal)
    add_dependencies(${target} ${target}_${arg_NAMESPACE})
    target_sources(${target} PRIVATE "${source}")
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${dir}/include>")
endfunction()
