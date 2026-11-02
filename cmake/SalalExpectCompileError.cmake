# cmake -DBUILD_DIR=<dir> -DCONFIG=<config> -DTARGET=<target> -DEXPECT=<text> -P SalalExpectCompileError.cmake
#
# Builds <target>, which is meant not to compile, and passes only if the build fails with <text> in
# its output, so a target that breaks for some other reason fails the test too.
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target "${TARGET}" --config "${CONFIG}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
if(result EQUAL 0)
    message(FATAL_ERROR "${TARGET} compiled; it was meant to fail on ${EXPECT}")
endif()
if(NOT output MATCHES "${EXPECT}")
    message(FATAL_ERROR "${TARGET} failed without naming ${EXPECT}:\n${output}")
endif()
message(STATUS "${TARGET} failed to compile on ${EXPECT}, as it should")
