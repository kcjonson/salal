add_library(foundation
    Foundation.cpp
)

target_include_directories(foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The bottom layer: no salal library below it.
target_link_libraries(foundation
    PUBLIC
        glm::glm
)

salal_add_tests(foundation)
