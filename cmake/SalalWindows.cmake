# salal_windows_manifest(<target>)
#
# Declares the executable per-monitor DPI aware (V2) in its Windows manifest, so Windows never scales
# its output and the framebuffer is the monitor's own resolution at any display scale, which the ui's
# scale is computed from (docs/app-api.md, "UI scale"). GLFW asks for the same at glfwInit; the manifest
# holds from the process's first instruction, before a crash dialog or a splash can make a window.
# Does nothing off Windows. MSVC's linker merges a .manifest source into the manifest it embeds; with
# another toolchain the game embeds cmake/windows/dpi-aware.manifest through its own resource script.

set(_SALAL_WINDOWS_DIR "${CMAKE_CURRENT_LIST_DIR}/windows")

function(salal_windows_manifest target)
    if(NOT WIN32)
        return()
    endif()
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR "salal_windows_manifest: ${target} is a ${type}; a manifest goes on an executable")
    endif()
    if(NOT MSVC)
        message(WARNING
            "salal_windows_manifest: only MSVC merges a .manifest source; embed "
            "${_SALAL_WINDOWS_DIR}/dpi-aware.manifest in ${target} through a resource script")
        return()
    endif()
    target_sources(${target} PRIVATE "${_SALAL_WINDOWS_DIR}/dpi-aware.manifest")
endfunction()
