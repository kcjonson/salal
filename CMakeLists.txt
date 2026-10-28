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
include(cmake/SalalBaseShader.cmake)

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
# links between static libraries. Also the renderer's backend boundary: GL and
# GLFW are the renderer's private business, so it must not export them as link
# dependencies, and no public renderer header may reach a GL, GLES, OpenGL, GLFW
# or glad header (docs/renderer-boundary.md). The same holds for app, whose GLFW
# platform is the one place in it that names GLFW or GL (docs/app-api.md).
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
        # A static library's PRIVATE links appear in the interface as $<LINK_ONLY:...>,
        # which carries no include directories or compile usage; a plain entry would.
        if(lib STREQUAL "renderer")
            _salal_check_gl_private(renderer docs/renderer-boundary.md)
        endif()
    endforeach()
    salal_check_headers_gl_free(renderer "^backend/gl/" docs/renderer-boundary.md)
    if(TARGET salal_app)
        _salal_check_gl_private(app docs/app-api.md)
        salal_check_headers_gl_free(app "" docs/app-api.md)
    endif()
endfunction()

function(_salal_check_gl_private lib doc)
    get_target_property(iface salal_${lib} INTERFACE_LINK_LIBRARIES)
    foreach(dep IN LISTS iface)
        if(dep MATCHES "^(OpenGL::|GLEW::|glfw$)")
            message(FATAL_ERROR "${lib} must keep GL and GLFW private; it exports ${dep} (${doc}).")
        endif()
    endforeach()
endfunction()

# Every header under libs/<lib> whose path doesn't match `skip` is public: in the
# renderer, everything outside backend/gl/, backend/Backend.h and the null backend
# included (backend/gl/ is the GL backend and the explicit GL opt-in for a game's
# own passes, docs/renderer-boundary.md); in app, every header. Walk
# each one's quoted and angle includes transitively through this repo's libraries
# and fail on the first path that reaches a GL, GLES, OpenGL, GLFW or glad header
# (GL/glew.h, GL/gl.h, GLFW/glfw3.h, ...). Headers proven clean once are not walked again.
function(salal_check_headers_gl_free lib skip doc)
    set(lib_dir "${PROJECT_SOURCE_DIR}/libs/${lib}")
    file(GLOB lib_roots LIST_DIRECTORIES true "${PROJECT_SOURCE_DIR}/libs/*")
    file(GLOB_RECURSE headers RELATIVE "${lib_dir}" CONFIGURE_DEPENDS "${lib_dir}/*.h")
    set(clean "")
    foreach(header IN LISTS headers)
        if(NOT skip STREQUAL "" AND header MATCHES "${skip}")
            continue()
        endif()
        set(visited "")
        _salal_walk_includes("${lib_dir}/${header}" "${header}" visited "${lib_roots}")
        list(APPEND clean ${visited})
        list(REMOVE_DUPLICATES clean)
    endforeach()
endfunction()

# Depth-first over one file's includes. `chain` is the include path so far, for the
# error message; `visited_var` names the caller's list of files already walked;
# `clean`, `lib` and `doc`, inherited from the caller, hold files already proven
# GL-free and name the library and the doc for the message.
function(_salal_walk_includes file chain visited_var roots)
    set(visited ${${visited_var}})
    if(file IN_LIST visited OR file IN_LIST clean)
        return()
    endif()
    list(APPEND visited "${file}")
    get_filename_component(dir "${file}" DIRECTORY)
    file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
    foreach(line IN LISTS lines)
        string(REGEX MATCH "#[ \t]*include[ \t]*([<\"])([^>\"]+)[>\"]" _ "${line}")
        set(form "${CMAKE_MATCH_1}")
        set(target "${CMAKE_MATCH_2}")
        if(target MATCHES "^(GL|GLES[23]?|OpenGL|GLFW|glad)/")
            message(FATAL_ERROR "Public ${lib} header reaches <${target}>: ${chain} (${doc}).")
        endif()
        set(candidates "")
        if(form STREQUAL "\"")
            list(APPEND candidates "${dir}/${target}")
        endif()
        foreach(root IN LISTS roots)
            list(APPEND candidates "${root}/${target}")
        endforeach()
        foreach(candidate IN LISTS candidates)
            if(EXISTS "${candidate}" AND NOT IS_DIRECTORY "${candidate}")
                get_filename_component(candidate "${candidate}" ABSOLUTE)
                _salal_walk_includes("${candidate}" "${chain} -> ${target}" visited "${roots}")
                break()
            endif()
        endforeach()
    endforeach()
    set(${visited_var} ${visited} PARENT_SCOPE)
endfunction()

salal_check_layering()
