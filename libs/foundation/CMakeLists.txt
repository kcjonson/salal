find_package(glm CONFIG REQUIRED)

add_library(salal_foundation STATIC
    Foundation.cpp
)
add_library(salal::foundation ALIAS salal_foundation)

target_compile_features(salal_foundation PUBLIC cxx_std_20)
target_include_directories(salal_foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# The bottom layer: no salal library below it.
target_link_libraries(salal_foundation
    PUBLIC
        glm::glm
)

set_target_properties(salal_foundation PROPERTIES FOLDER salal)

salal_add_tests(foundation)
