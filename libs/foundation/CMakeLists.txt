find_package(glm CONFIG REQUIRED)

add_library(salal_foundation STATIC
    debug/Tunables.cpp
    metrics/SystemResources.cpp
    threading/TaskPool.cpp
    utils/Log.cpp
    utils/ResourcePath.cpp
    utils/Utf8.cpp
)
add_library(salal::foundation ALIAS salal_foundation)

target_compile_features(salal_foundation PUBLIC cxx_std_20)

# Bit-identical floating point across x64 and arm64. DeterministicMath and HashNoise are
# header-inline Horner polynomials, and Clang fuses a*b+c into an FMA wherever the target
# has one (arm64 always, x64 only with -mfma); the golden bit patterns were computed without
# fusion and failed on the first macOS run. PUBLIC because the math is inlined into every
# TU that includes the headers. MSVC's default /fp:precise does not contract.
target_compile_options(salal_foundation PUBLIC
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>>:-ffp-contract=off>
)

# The library directory is the include root: consumers write <utils/Log.h>,
# <math/Types.h>, the spellings worldsim has always used.
target_include_directories(salal_foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# PUBLIC and per-config: Log.h compiles LOG_DEBUG/INFO/WARNING out without it, and
# every TU that includes Log.h has to agree with the library. A generator
# expression, not if(CMAKE_BUILD_TYPE), because multi-config generators leave
# CMAKE_BUILD_TYPE empty.
target_compile_definitions(salal_foundation PUBLIC $<$<CONFIG:Debug,RelWithDebInfo>:DEVELOPMENT_BUILD>)

# The bottom layer: glm only (math/Types.h exposes glm types). No GL, no sockets.
target_link_libraries(salal_foundation PUBLIC glm::glm)

set_target_properties(salal_foundation PROPERTIES FOLDER salal)

salal_add_tests(foundation)
