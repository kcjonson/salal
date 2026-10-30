# salal_tokens(<target> <dir> [NAMESPACE <name>] [HEADER <file>])
#
# A theme's tokens for <target>, checked and compiled in at build time (docs/ui-design.md, "Theme"):
# every *.tokens.json in <dir> and at most one *.resolver.json, in the Design Tokens Format Module
# 2025.10 with its Resolver module, loaded over ui's own tokens in every mode the resolver has. The
# header, HEADER (default Tokens.h, a relative path, folders allowed) on <target>'s include path,
# declares <name>::styles, a typed ui::StyleId for each group under `styles`, <name>::tokens, a
# typed handle for every other token, and <name>::tokenTable(), the table ThemeArgs takes; a
# misspelt style is a compile error. The files are embedded, so nothing sits beside the executable.
# NAMESPACE defaults to <target>. A wrong $type, a broken alias, a style field that isn't one, or a
# token some modes have and others lack fails the build, naming the file and the token; entries
# whose names start with a dot are left out. <target> links salal::ui. The step is the target
# <target>_tokens, which <target> waits for.
function(salal_tokens target dir)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "NAMESPACE;HEADER" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "salal_tokens(${target}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(arg_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "salal_tokens(${target}): ${arg_KEYWORDS_MISSING_VALUES} given without a value")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "salal_tokens: ${target} is not a target")
    endif()
    if(NOT TARGET salal_tokens_tool)
        message(FATAL_ERROR "salal_tokens(${target}): salal's ui isn't built; set SALAL_BUILD_UI before adding salal")
    endif()
    if(NOT DEFINED arg_NAMESPACE)
        set(arg_NAMESPACE ${target})
    endif()
    if(NOT arg_NAMESPACE MATCHES "^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
        message(FATAL_ERROR "salal_tokens(${target}): NAMESPACE ${arg_NAMESPACE} is not a C++ namespace name")
    endif()
    if(NOT DEFINED arg_HEADER)
        set(arg_HEADER Tokens.h)
    endif()
    if(IS_ABSOLUTE "${arg_HEADER}" OR arg_HEADER MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "salal_tokens(${target}): HEADER ${arg_HEADER} must be a path inside the include folder")
    endif()
    get_property(called TARGET ${target} PROPERTY SALAL_TOKENS SET)
    if(called)
        message(FATAL_ERROR "salal_tokens(${target}): called twice; a target has one theme, so its tokens live in one folder")
    endif()
    set_property(TARGET ${target} PROPERTY SALAL_TOKENS ON)

    get_filename_component(tokens_dir "${dir}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${tokens_dir}")
        message(FATAL_ERROR "salal_tokens(${target}): ${dir} is not a folder")
    endif()
    get_filename_component(ui_tokens "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../libs/ui/tokens" ABSOLUTE)
    set(base "")
    if(NOT tokens_dir STREQUAL ui_tokens)
        if(arg_NAMESPACE STREQUAL "ui")
            message(FATAL_ERROR "salal_tokens(${target}): NAMESPACE ui is ui's own; give the game's tokens their own namespace")
        endif()
        set(base "${ui_tokens}")
    endif()

    # The tool lists the folders itself; the globs are for DEPENDS and for reconfiguring when a file
    # comes or goes.
    file(GLOB_RECURSE inputs CONFIGURE_DEPENDS "${tokens_dir}/*")
    if(base)
        file(GLOB_RECURSE base_inputs CONFIGURE_DEPENDS "${base}/*")
        list(APPEND inputs ${base_inputs})
    endif()
    list(FILTER inputs EXCLUDE REGEX "/\\.[^/]*$")

    get_filename_component(stem "${arg_HEADER}" NAME_WLE)
    set(out "${CMAKE_CURRENT_BINARY_DIR}/salal_tokens/${target}")
    set(header "${out}/include/${arg_HEADER}")
    set(source "${out}/${stem}.cpp")
    add_custom_command(
        OUTPUT "${header}" "${source}"
        COMMAND salal_tokens_tool "${tokens_dir}" "${arg_NAMESPACE}" "${arg_HEADER}" "${header}" "${source}" ${base}
        DEPENDS salal_tokens_tool ${inputs}
        COMMENT "Checking and compiling the tokens for ${target} (${arg_NAMESPACE})"
        VERBATIM
    )
    add_custom_target(${target}_tokens DEPENDS "${header}" "${source}")
    set_target_properties(${target}_tokens PROPERTIES FOLDER salal)
    add_dependencies(${target} ${target}_tokens)
    target_sources(${target} PRIVATE "${source}")
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${out}/include>")
endfunction()
