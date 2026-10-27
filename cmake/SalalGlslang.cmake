# cmake -DGLSLANG=<glslangValidator> -DDIR=<dir> -P SalalGlslang.cmake
#
# salal_base_shader()'s check: <dir>/base.vert and base.frag compiled and linked as one program
# (stage interfaces included), and glslangValidator's reflection report written to
# <dir>/reflection.txt, where the hooks' block sizes come from. A stage that does not compile, or
# two that do not link, fails the build with glslang's errors.
execute_process(
    COMMAND "${GLSLANG}" -l -q "${DIR}/base.vert" "${DIR}/base.frag"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
if(NOT result EQUAL 0)
    # What follows the errors is an empty reflection report.
    string(FIND "${output}" "Uniform reflection:" report)
    if(report GREATER -1)
        string(SUBSTRING "${output}" 0 ${report} output)
    endif()
    message(FATAL_ERROR
        "salal_base_shader: glslangValidator rejects the assembled base shader. Its line numbers count in "
        "${DIR}/base.vert and base.frag, where each hook's start and end are marked.\n${output}${errors}")
endif()
file(WRITE "${DIR}/reflection.txt" "${output}")
