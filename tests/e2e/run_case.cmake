# run_case.cmake - run one example program and check what it did
#
#   cmake -DEXE=<program> -DARGS=<a|b|c> -DEXPECT_CODE=<n>
#         [-DEXPECT_OUT=<regex>] [-DEXPECT_ERR=<regex>]
#         [-DENV=<NAME=VALUE|...>] -P run_case.cmake
#
# ARGS and ENV use '|' as separator, because add_test() would split a
# ';' list into separate words. An empty EXPECT_OUT / EXPECT_ERR is not
# checked; use "^$" to require empty output. The regexes are CMake
# regular expressions matched anywhere in the stream.

if(NOT DEFINED EXE OR NOT DEFINED EXPECT_CODE)
    message(FATAL_ERROR "run_case.cmake: EXE and EXPECT_CODE are required")
endif()

set(argv "")
if(NOT "${ARGS}" STREQUAL "")
    string(REPLACE "|" ";" argv "${ARGS}")
endif()

if(NOT "${ENV}" STREQUAL "")
    string(REPLACE "|" ";" env_list "${ENV}")
    foreach(kv IN LISTS env_list)
        string(FIND "${kv}" "=" eq)
        if(eq LESS 1)
            message(FATAL_ERROR "run_case.cmake: bad ENV entry '${kv}'")
        endif()
        string(SUBSTRING "${kv}" 0 ${eq} name)
        math(EXPR vpos "${eq} + 1")
        string(SUBSTRING "${kv}" ${vpos} -1 value)
        set(ENV{${name}} "${value}")
    endforeach()
endif()

execute_process(
    COMMAND "${EXE}" ${argv}
    RESULT_VARIABLE code
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)

set(problems "")
if(NOT "${code}" STREQUAL "${EXPECT_CODE}")
    string(APPEND problems "  exit code ${code}, expected ${EXPECT_CODE}\n")
endif()
if(NOT "${EXPECT_OUT}" STREQUAL "" AND NOT "${out}" MATCHES "${EXPECT_OUT}")
    string(APPEND problems "  stdout does not match: ${EXPECT_OUT}\n")
endif()
if(NOT "${EXPECT_ERR}" STREQUAL "" AND NOT "${err}" MATCHES "${EXPECT_ERR}")
    string(APPEND problems "  stderr does not match: ${EXPECT_ERR}\n")
endif()

if(NOT problems STREQUAL "")
    string(REPLACE ";" " " shown "${argv}")
    message(FATAL_ERROR "e2e case failed: ${EXE} ${shown}\n"
        "${problems}"
        "--- stdout ---\n${out}"
        "--- stderr ---\n${err}"
        "--------------")
endif()
message(STATUS "ok: exit ${code}")
