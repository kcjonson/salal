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

# Refuse Visual Studio's bundled vcpkg. A developer shell (vcvars64, VsDevCmd) sets
# VCPKG_ROOT to it, over your own checkout, and the preset's toolchain follows: no
# binary cache, every port built from source. vcpkg installs the manifest's ports
# inside project(), so the check sits before it (PROJECT_IS_TOP_LEVEL isn't set yet,
# hence the source dir comparison). Top-level only: a game pulling salal in through
# FetchContent runs its own.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND CMAKE_TOOLCHAIN_FILE)
    block(SCOPE_FOR VARIABLES)
        string(REPLACE "\\" "/" toolchain "${CMAKE_TOOLCHAIN_FILE}")
        string(TOLOWER "${toolchain}" toolchain_lower)
        if(toolchain_lower MATCHES "^(.*/microsoft visual studio/.+/vc/vcpkg)(/|$)")
            string(LENGTH "${CMAKE_MATCH_1}" bundled_length)
            string(SUBSTRING "${toolchain}" 0 ${bundled_length} bundled)
            message(FATAL_ERROR
                "CMAKE_TOOLCHAIN_FILE is in Visual Studio's bundled vcpkg (${bundled}); a developer shell sets VCPKG_ROOT to it.\n"
                "Set VCPKG_ROOT to your own vcpkg checkout, then configure with --fresh or delete the build directory.")
        endif()
    endblock()
endif()

project(salal VERSION 0.1.0 LANGUAGES CXX)

# Everything on for the standalone build, everything off when a game pulls salal
# in through FetchContent, so a consumer builds only the layers it links.
option(SALAL_BUILD_UI "Build salal_ui" ${PROJECT_IS_TOP_LEVEL})
option(SALAL_BUILD_APP "Build salal_app (implies SALAL_BUILD_UI, which app hosts)" ${PROJECT_IS_TOP_LEVEL})
option(SALAL_BUILD_SANDBOX "Build the sandbox app (implies SALAL_BUILD_APP)" ${PROJECT_IS_TOP_LEVEL})
option(SALAL_BUILD_TESTS "Build salal's tests and benchmarks" ${PROJECT_IS_TOP_LEVEL})

# app's HTTP dev server (docs/app-api.md, "Dev server"): compiled into Debug and RelWithDebInfo unless
# SALAL_WITH_DEV_SERVER is off, and into Release only for a profiling build. No listening socket in
# a shipped build. Ninja Multi-Config and Visual Studio build every configuration from one
# configure, so the choice is a generator expression, SALAL_DEV_SERVER_GENEX, true in the
# configurations that have the server; app defines SALAL_DEV_SERVER and links cpp-httplib only there.
option(SALAL_WITH_DEV_SERVER "Compile app's dev server into Debug and RelWithDebInfo" ON)
option(SALAL_DEV_SERVER_IN_RELEASE "Compile app's dev server into Release too, for profiling" OFF)
if(SALAL_WITH_DEV_SERVER AND SALAL_DEV_SERVER_IN_RELEASE)
    set(SALAL_DEV_SERVER_GENEX "$<CONFIG:Debug,RelWithDebInfo,Release>")
elseif(SALAL_WITH_DEV_SERVER)
    set(SALAL_DEV_SERVER_GENEX "$<CONFIG:Debug,RelWithDebInfo>")
else()
    set(SALAL_DEV_SERVER_GENEX "0")
endif()

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
include(cmake/SalalTokens.cmake)

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
# platform is the one place in it that names GLFW or GL (docs/app-api.md). And
# cpp-httplib is app's dev server's alone: no lower layer links it, no public header
# reaches it, and app links it only where SALAL_DEV_SERVER is defined.
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
            if(lib STREQUAL "foundation" AND dep MATCHES "(^|[:<])(OpenGL::|GLEW::|glfw>?$)")
                message(FATAL_ERROR "foundation must stay GL-free; it links ${dep}.")
            endif()
            if(dep MATCHES "httplib")
                message(FATAL_ERROR "salal_${lib} links ${dep}; cpp-httplib is app's dev server's alone (docs/app-api.md).")
            endif()
        endforeach()
        # A static library's PRIVATE links appear in the interface as $<LINK_ONLY:...>,
        # which carries no include directories or compile usage; a plain entry would.
        if(lib STREQUAL "renderer")
            _salal_check_gl_private(renderer docs/renderer-boundary.md)
        endif()
    endforeach()
    salal_check_public_headers(renderer "^backend/gl/" docs/renderer-boundary.md)
    if(TARGET salal_ui)
        _salal_check_gl_private(ui docs/ui-design.md)
        salal_check_public_headers(ui "" docs/ui-design.md)
    endif()
    if(TARGET salal_app)
        _salal_check_gl_private(app docs/app-api.md)
        _salal_check_dev_server_link()
        salal_check_public_headers(app "" docs/app-api.md)
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

# Every app link that names cpp-httplib carries SALAL_DEV_SERVER_GENEX, the condition
# SALAL_DEV_SERVER is defined under, so a configuration without the server links none of
# it; with SALAL_WITH_DEV_SERVER off, nothing names it. A static library's PRIVATE link
# shows in its interface wrapped in $<LINK_ONLY:...>.
function(_salal_check_dev_server_link)
    set(guarded "$<${SALAL_DEV_SERVER_GENEX}:httplib::httplib>")
    get_target_property(direct salal_app LINK_LIBRARIES)
    get_target_property(iface salal_app INTERFACE_LINK_LIBRARIES)
    foreach(dep IN LISTS direct iface)
        if(dep MATCHES "httplib" AND (NOT SALAL_WITH_DEV_SERVER OR NOT (dep STREQUAL guarded OR dep STREQUAL "$<LINK_ONLY:${guarded}>")))
            message(FATAL_ERROR "app links ${dep}; cpp-httplib goes only where the dev server is compiled in, as ${guarded} (docs/app-api.md).")
        endif()
    endforeach()
endfunction()

# Every header under libs/<lib> whose path doesn't match `skip` is public: in the
# renderer, everything outside backend/gl/, backend/Backend.h and the null backend
# included (backend/gl/ is the GL backend and the explicit GL opt-in for a game's
# own passes, docs/renderer-boundary.md); in ui and app, every header. Walk
# each one's quoted and angle includes transitively through this repo's libraries
# and fail on the first path that reaches a GL, GLES, OpenGL, GLFW or glad header
# (GL/glew.h, GL/gl.h, GLFW/glfw3.h, ...), or cpp-httplib's, which would put the dev
# server's sockets in every consumer's translation units. Headers proven clean once are
# not walked again.
function(salal_check_public_headers lib skip doc)
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
        if(target MATCHES "^(GL|GLES[23]?|OpenGL|GLFW|glad)/" OR target STREQUAL "httplib.h")
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
