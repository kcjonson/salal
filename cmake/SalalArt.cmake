# salal_art(<target> <dir> [NAMESPACE <name>] [HEADER <file>])
#
# A game's vector art for <target>, embedded at build time the way salal_icons() and salal_tokens()
# embed theirs, so nothing is staged beside the executable or found at run time. Every SVG in <dir>
# is checked against what renderer::loadSVG reads through nanosvg (renderer/vector/ArtBuild.h):
# paths and basic shapes in groups, painted in colours or gradients. A filter, mask, clip path,
# text, image, pattern, marker, <use>, <symbol>, <style> sheet, dash array or reference, or anything
# in <dir> that isn't an SVG, fails the build naming the file, line and element; entries whose
# names start with a dot are left out. The header, HEADER (default Art.h) on <target>'s include
# path, declares <name>::art(name), a file's bytes for loadSVG's span overload (art/tape.svg is
# "tape"), and <name>::artNames(). NAMESPACE defaults to <target>. The step is the target
# <target>_art, which <target> waits for.
function(salal_art target dir)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "NAMESPACE;HEADER" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "salal_art(${target}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(arg_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "salal_art(${target}): ${arg_KEYWORDS_MISSING_VALUES} given without a value")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "salal_art: ${target} is not a target")
    endif()
    if(NOT TARGET salal_art_tool)
        message(FATAL_ERROR "salal_art(${target}): the art tool isn't built; it comes with salal's renderer")
    endif()
    if(NOT DEFINED arg_NAMESPACE)
        set(arg_NAMESPACE ${target})
    endif()
    if(NOT arg_NAMESPACE MATCHES "^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
        message(FATAL_ERROR "salal_art(${target}): NAMESPACE ${arg_NAMESPACE} is not a C++ namespace name")
    endif()
    if(NOT DEFINED arg_HEADER)
        set(arg_HEADER Art.h)
    endif()
    if(IS_ABSOLUTE "${arg_HEADER}" OR arg_HEADER MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "salal_art(${target}): HEADER ${arg_HEADER} must be a path inside the include folder")
    endif()
    get_property(called TARGET ${target} PROPERTY SALAL_ART SET)
    if(called)
        message(FATAL_ERROR "salal_art(${target}): called twice; a target's art lives in one folder")
    endif()
    set_property(TARGET ${target} PROPERTY SALAL_ART ON)

    get_filename_component(art_dir "${dir}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${art_dir}")
        message(FATAL_ERROR "salal_art(${target}): ${dir} is not a folder")
    endif()
    # The tool lists the folder itself; the glob is for DEPENDS (folders too, so the tool can
    # refuse them) and for reconfiguring when a file comes or goes.
    file(GLOB inputs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${art_dir}/*")
    list(FILTER inputs EXCLUDE REGEX "/\\.[^/]*$")

    get_filename_component(stem "${arg_HEADER}" NAME_WLE)
    set(out "${CMAKE_CURRENT_BINARY_DIR}/salal_art/${target}")
    set(header "${out}/include/${arg_HEADER}")
    set(source "${out}/${stem}.cpp")
    add_custom_command(
        OUTPUT "${header}" "${source}"
        COMMAND salal_art_tool "${art_dir}" "${arg_NAMESPACE}" "${arg_HEADER}" "${header}" "${source}"
        DEPENDS salal_art_tool ${inputs}
        COMMENT "Embedding the vector art for ${target} (${arg_NAMESPACE})"
        VERBATIM
    )
    add_custom_target(${target}_art DEPENDS "${header}" "${source}")
    set_target_properties(${target}_art PROPERTIES FOLDER salal)
    add_dependencies(${target} ${target}_art)
    target_sources(${target} PRIVATE "${source}")
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${out}/include>")
endfunction()
