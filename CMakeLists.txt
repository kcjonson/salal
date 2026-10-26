# The dependency rule (docs/architecture.md). A library may depend only on layers
# below it, and that is enforced here through target_link_libraries, not by
# convention. No cycles, nothing upward. salal_check_layering() at the bottom of
# this file checks it at configure time.
#
#   app
#    |
#   ui
#    |
#   renderer, foundation      (renderer -> foundation)
#
# A consuming game's simulation layer may depend on neither renderer nor ui.

# 3.25 for CMAKE_MSVC_DEBUG_INFORMATION_FORMAT (CMP0141): the Windows preset
# selects /Z7 embedded debug info, which ccache requires to cache MSVC objects.
cmake_minimum_required(VERSION 3.25)
project(salal VERSION 0.1.0 LANGUAGES CXX)

# Everything on for the standalone build, everything off when a game pulls salal
# in through FetchContent, so a consumer builds only the layers it links.
option(SALAL_BUILD_UI "Build salal_ui" ${PROJECT_IS_TOP_LEVEL})
option(SALAL_BUILD_APP "Build salal_app (implies SALAL_BUILD_UI)" ${PROJECT_IS_TOP_LEVEL})
option(SALAL_BUILD_SANDBOX "Build the sandbox app (implies SALAL_BUILD_APP)" ${PROJECT_IS_TOP_LEVEL})
option(SALAL_BUILD_TESTS "Build salal's tests and benchmarks" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC enforces C++20 declaration order for designated initializers (C7560);
# the matching error on Clang catches the same portability break locally.
# Feature-detected because AppleClang and GCC don't all know the warning group.
if(MSVC)
    add_compile_options(/utf-8)
else()
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-Werror=reorder-init-list" SALAL_HAS_WERROR_REORDER_INIT_LIST)
    if(SALAL_HAS_WERROR_REORDER_INIT_LIST)
        add_compile_options(-Werror=reorder-init-list)
    endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(PROJECT_IS_TOP_LEVEL)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

if(SALAL_BUILD_TESTS)
    include(CTest)
endif()
include(cmake/SalalTesting.cmake)

add_subdirectory(libs/foundation)
add_subdirectory(libs/renderer)
if(SALAL_BUILD_UI OR SALAL_BUILD_APP OR SALAL_BUILD_SANDBOX)
    add_subdirectory(libs/ui)
endif()
if(SALAL_BUILD_APP OR SALAL_BUILD_SANDBOX)
    add_subdirectory(libs/app)
endif()
if(SALAL_BUILD_SANDBOX)
    add_subdirectory(apps/sandbox)
endif()

# salal_check_layering(): the dependency rule at configure time. Matches the plain
# target, the salal:: alias, and the $<LINK_ONLY:...> form CMake adds for PRIVATE
# links between static libraries.
function(salal_check_layering)
    set(above_foundation renderer ui app)
    set(above_renderer ui app)
    set(above_ui app)
    foreach(lib foundation renderer ui)
        if(NOT TARGET salal_${lib})
            continue()
        endif()
        get_target_property(direct salal_${lib} LINK_LIBRARIES)
        get_target_property(iface salal_${lib} INTERFACE_LINK_LIBRARIES)
        foreach(dep IN LISTS direct iface)
            foreach(upper IN LISTS above_${lib})
                if(dep MATCHES "(^|[:<])salal(_|::)${upper}>?$")
                    message(FATAL_ERROR "Dependency rule: salal_${lib} links ${upper}, a layer above it (docs/architecture.md).")
                endif()
            endforeach()
            if(lib STREQUAL "foundation" AND dep MATCHES "(^|[:<])(OpenGL::|GLEW::|glfw>?$|httplib::)")
                message(FATAL_ERROR "foundation must stay GL-free and socket-free; it links ${dep}.")
            endif()
        endforeach()
    endforeach()
endfunction()

salal_check_layering()
