# The dependency rule (docs/architecture.md). A library may depend only on layers
# below it, and that is enforced here through target_link_libraries, not by
# convention. No cycles, nothing upward.
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
    check_cxx_compiler_flag("-Werror=reorder-init-list" HAS_WERROR_REORDER_INIT_LIST)
    if(HAS_WERROR_REORDER_INIT_LIST)
        add_compile_options(-Werror=reorder-init-list)
    endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(glm CONFIG REQUIRED)

include(CTest)
include(cmake/SalalTesting.cmake)

add_subdirectory(libs/foundation)
add_subdirectory(libs/renderer)
add_subdirectory(libs/ui)
add_subdirectory(libs/app)

add_subdirectory(apps/sandbox)
