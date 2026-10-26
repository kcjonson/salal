# salal_add_tests(<lib>): builds every *.test.cpp under the calling directory
# into <lib>-tests, linked against <lib> and gtest, and registers it with CTest.
function(salal_add_tests lib)
    if(NOT BUILD_TESTING)
        return()
    endif()

    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp")
    if(NOT test_sources)
        return()
    endif()

    find_package(GTest CONFIG REQUIRED)
    add_executable(${lib}-tests ${test_sources})
    target_link_libraries(${lib}-tests
        PRIVATE
            ${lib}
            GTest::gtest
            GTest::gtest_main
    )
    add_test(NAME ${lib}-tests COMMAND ${lib}-tests)
endfunction()
