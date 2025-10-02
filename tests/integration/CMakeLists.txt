# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

cmake_minimum_required(VERSION 3.16)
project(TestScripts LANGUAGES NONE) # silence the warning

if(WITH_VALGRIND)
    if(NOT VALGRIND)
        message(WARNING "WITH_VALGRIND is on, but VALGRIND variable is empty. Valgrind will be DISABLED in integration tests!")
    endif()

    # just inlining ${VALGRIND} results in semicolons instead of spaces
    string(REPLACE ";" " " VALGRIND_ESC "${VALGRIND}")
    macro(add_valgrind TEST_NAME)
        set_property(TEST ${TEST_NAME} APPEND PROPERTY ENVIRONMENT "VALGRIND=${VALGRIND_ESC}")
    endmacro()
else()
    macro(add_valgrind)
    endmacro()
endif()

### FindPython3
if(NOT TARGET Python3::Interpreter)
    include(../../cmake/requirePython3venv.cmake)
endif()

set(INTEGRATION_TEST_PREFIX "test_demo_")

if(TEST_KEEP_SUCCESS_LOGS)
    set(ADDITIONAL_RUNTEST_ARGS "--keep-success-logs")
else()
    set(ADDITIONAL_RUNTEST_ARGS)
endif()

execute_process(COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/runtest.py" "-l"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                RESULT_VARIABLE RUNTEST_LIST_RESULT
                OUTPUT_VARIABLE RUNTEST_LIST_OUTPUT)

if(NOT RUNTEST_LIST_RESULT EQUAL 0)
    message(FATAL_ERROR "runtest.py -l failed")
endif()

string(REPLACE "\n" ";" RUNTEST_LIST_OUTPUT "${RUNTEST_LIST_OUTPUT}")
list(SORT RUNTEST_LIST_OUTPUT)
foreach(TEST_CASE_ENTRY IN LISTS RUNTEST_LIST_OUTPUT)
    if(TEST_CASE_ENTRY MATCHES "^\\* .*$")
        string(REGEX REPLACE "^\\* " "" TEST_CASE_ENTRY "${TEST_CASE_ENTRY}")
        add_test(${INTEGRATION_TEST_PREFIX}${TEST_CASE_ENTRY}
                 ${CMAKE_CURRENT_SOURCE_DIR}/runtest.py "^${TEST_CASE_ENTRY}\\\$"
                                                        --client=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/demo
                                                        ${ADDITIONAL_RUNTEST_ARGS})

        # The longest test is 5 minutes, so set the timeout to 6 minutes
        set_property(TEST ${INTEGRATION_TEST_PREFIX}${TEST_CASE_ENTRY} PROPERTY TIMEOUT 360)

        add_valgrind(${INTEGRATION_TEST_PREFIX}${TEST_CASE_ENTRY})
    endif()
endforeach()

if(NOT MBEDTLS_LIBRARY)
    message(FATAL_ERROR "mbed TLS is required for integration tests")
endif()

set(PYMBEDTLS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/test-framework-tools/pymbedtls")

add_custom_target(pymbedtls COMMAND
                  env "MBEDTLS_ROOT_DIR=${MBEDTLS_ROOT_DIR}"
                  python3 -m pip install "${PYMBEDTLS_SRC_DIR}")

set(WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
configure_file("run_tests.sh.in" "run_tests.sh")
add_custom_target(integration_check
                  COMMAND ./run_tests.sh
                  DEPENDS demo pymbedtls)
add_custom_target(integration_check_hsm
                  COMMAND ./run_tests.sh -h
                  DEPENDS demo pymbedtls)
add_dependencies(check integration_check)
