# salal_add_tests(<lib>): builds every *.test.cpp under the calling directory
# into salal_<lib>_tests, linked against salal::<lib> and gtest, and registers
# it with CTest. Every *.bench.cpp becomes salal_<lib>_benchmarks against
# Google Benchmark, built but not registered. Nothing happens unless salal is
# building its own tests, so a consumer never pays for gtest.
function(salal_add_tests lib)
    if(NOT (SALAL_BUILD_TESTS AND BUILD_TESTING))
        return()
    endif()

    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp")
    if(test_sources)
        find_package(GTest CONFIG REQUIRED)
        add_executable(salal_${lib}_tests ${test_sources})
        target_link_libraries(salal_${lib}_tests
            PRIVATE
                salal::${lib}
                GTest::gtest
                GTest::gtest_main
        )
        set_target_properties(salal_${lib}_tests PROPERTIES FOLDER salal/tests)
        add_test(NAME salal_${lib}_tests COMMAND salal_${lib}_tests)
    endif()

    file(GLOB_RECURSE bench_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp")
    if(bench_sources)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(salal_${lib}_benchmarks ${bench_sources})
        target_link_libraries(salal_${lib}_benchmarks
            PRIVATE
                salal::${lib}
                benchmark::benchmark
                benchmark::benchmark_main
        )
        set_target_properties(salal_${lib}_benchmarks PROPERTIES FOLDER salal/benchmarks)
    endif()
endfunction()
