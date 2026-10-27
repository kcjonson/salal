# cmake -DGLSLANG=<glslangValidator> -DDIR=<dir> -P SalalGlslang.cmake
#
# salal_base_shader()'s check: <dir>/base.vert and base.frag compiled and linked as one program
# (stage interfaces included), and glslangValidator's reflection report, every member of every
# block, written to <dir>/reflection.txt, where the hooks' block sizes and member offsets come
# from. A stage that does not compile, two that do not link, or any warning fails the build with
# glslang's output.
if(NOT EXISTS "${GLSLANG}")
    message(FATAL_ERROR
        "salal_base_shader: glslangValidator is not at ${GLSLANG} any more (vcpkg_installed was cleared?). "
        "Reconfigure, or set SALAL_GLSLANG_VALIDATOR.")
endif()
execute_process(
    COMMAND "${GLSLANG}" -l -q --reflect-all-block-variables "${DIR}/base.vert" "${DIR}/base.frag"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
# What glslang says before its reflection report, less the two file names it echoes.
string(REPLACE "\r" "" said "${output}")
string(FIND "${said}" "Uniform reflection:" report)
if(report GREATER -1)
    string(SUBSTRING "${said}" 0 ${report} said)
endif()
string(REPLACE "${DIR}/base.vert\n" "" said "${said}")
string(REPLACE "${DIR}/base.frag\n" "" said "${said}")
string(STRIP "${said}${errors}" said)
if(NOT result EQUAL 0 OR NOT said STREQUAL "")
    message(FATAL_ERROR
        "salal_base_shader: glslangValidator rejects the assembled base shader (a warning counts). Its line numbers "
        "count in ${DIR}/base.vert and base.frag, where each hook's start and end are marked.\n${said}")
endif()
file(WRITE "${DIR}/reflection.txt" "${output}")
