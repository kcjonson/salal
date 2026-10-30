# salal_icons(<target> <dir> [NAMESPACE <name>] [HEADER <file>] [CELL <px>] [RANGE <px>])
#
# A game's icons for <target>, built at build time (docs/ui-design.md, "Icons"): every SVG in <dir>
# checked against the icon rules (a square viewBox; filled path, rect, circle and polygon only; one
# colour; no gradients, masks, clip paths, filters, text, images or references) and turned into one
# MSDF atlas by msdfgen, CELL px a cell (default 48) with a distance range of RANGE px (default 8).
# The header, HEADER (default Icons.h) on <target>'s include path, declares <name>::iconAtlas(), the
# atlas PNG, its msdf-atlas-gen JSON and the name table from file name to private-use codepoint,
# all embedded, so nothing sits beside the executable; icons/home.svg is the icon "home". The PNG
# and JSON are also written beside the generated source to look at. A file that breaks a rule, or
# anything in <dir> that isn't an SVG, fails the build naming the file, line and element; entries
# whose names start with a dot are left out. NAMESPACE defaults to <target>. <target> links
# salal::ui. The step is the target <target>_icons, which <target> waits for.
#
# Needs msdfgen: the vcpkg port msdfgen without its default features, as a host dependency.
function(salal_icons target dir)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "NAMESPACE;HEADER;CELL;RANGE" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "salal_icons(${target}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(arg_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "salal_icons(${target}): ${arg_KEYWORDS_MISSING_VALUES} given without a value")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "salal_icons: ${target} is not a target")
    endif()
    if(NOT TARGET salal_icons_tool)
        message(FATAL_ERROR
            "salal_icons(${target}): the icon tool isn't built. It needs salal's ui (SALAL_BUILD_UI) and msdfgen: add the vcpkg "
            "port msdfgen, with \"default-features\": false, to the manifest as a host dependency.")
    endif()
    if(NOT DEFINED arg_NAMESPACE)
        set(arg_NAMESPACE ${target})
    endif()
    if(NOT arg_NAMESPACE MATCHES "^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
        message(FATAL_ERROR "salal_icons(${target}): NAMESPACE ${arg_NAMESPACE} is not a C++ namespace name")
    endif()
    if(NOT DEFINED arg_HEADER)
        set(arg_HEADER Icons.h)
    endif()
    if(IS_ABSOLUTE "${arg_HEADER}" OR arg_HEADER MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "salal_icons(${target}): HEADER ${arg_HEADER} must be a path inside the include folder")
    endif()
    if(NOT DEFINED arg_CELL)
        set(arg_CELL 48)
    endif()
    if(NOT DEFINED arg_RANGE)
        set(arg_RANGE 8)
    endif()
    if(NOT arg_CELL MATCHES "^[1-9][0-9]*$" OR NOT arg_RANGE MATCHES "^[1-9][0-9]*$" OR NOT arg_CELL GREATER arg_RANGE)
        message(FATAL_ERROR "salal_icons(${target}): CELL and RANGE are whole px, CELL larger than RANGE")
    endif()
    get_property(called TARGET ${target} PROPERTY SALAL_ICONS SET)
    if(called)
        message(FATAL_ERROR "salal_icons(${target}): called twice; a target's icons live in one folder")
    endif()
    set_property(TARGET ${target} PROPERTY SALAL_ICONS ON)

    get_filename_component(icons_dir "${dir}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${icons_dir}")
        message(FATAL_ERROR "salal_icons(${target}): ${dir} is not a folder")
    endif()
    # The tool lists the folder itself; the glob is for DEPENDS (folders too, so the tool can
    # refuse them) and for reconfiguring when a file comes or goes.
    file(GLOB inputs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${icons_dir}/*")
    list(FILTER inputs EXCLUDE REGEX "/\\.[^/]*$")

    get_filename_component(stem "${arg_HEADER}" NAME_WLE)
    set(out "${CMAKE_CURRENT_BINARY_DIR}/salal_icons/${target}")
    set(header "${out}/include/${arg_HEADER}")
    set(source "${out}/${stem}.cpp")
    add_custom_command(
        OUTPUT "${header}" "${source}" "${out}/icons.png" "${out}/icons.json"
        COMMAND salal_icons_tool "${icons_dir}" "${out}" "${arg_NAMESPACE}" "${arg_HEADER}" "${header}" "${source}" ${arg_CELL} ${arg_RANGE}
        DEPENDS salal_icons_tool ${inputs}
        COMMENT "Building the icon atlas for ${target} (${arg_NAMESPACE})"
        VERBATIM
    )
    add_custom_target(${target}_icons DEPENDS "${header}" "${source}")
    set_target_properties(${target}_icons PROPERTIES FOLDER salal)
    add_dependencies(${target} ${target}_icons)
    target_sources(${target} PRIVATE "${source}")
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${out}/include>")
endfunction()
