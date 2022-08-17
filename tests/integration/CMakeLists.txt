# Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

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

set(DEFAULT_TEST_SUITE "default")
set(INTEGRATION_TEST_SUITE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/suites")
set(INTEGRATION_TEST_PREFIX "test_demo_")
set(PYMBEDTLS_MODULE_DIR ${ANJAY_BUILD_OUTPUT_DIR}/pymbedtls)

file(GLOB_RECURSE DEMO_TESTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/suites "${INTEGRATION_TEST_SUITE_ROOT}/*.py")
list(SORT DEMO_TESTS)
foreach(DEMO_TEST ${DEMO_TESTS})
    string(REGEX REPLACE "^${CMAKE_CURRENT_SOURCE_DIR}" "" DEMO_TEST "${DEMO_TEST}")
    string(REGEX REPLACE "\\.py" "" DEMO_TEST "${DEMO_TEST}")
    string(REGEX REPLACE "/" "." DEMO_TEST "${DEMO_TEST}")

    if (NOT DEMO_TEST MATCHES "__init__$")
        add_test(${INTEGRATION_TEST_PREFIX}${DEMO_TEST}
                 ${CMAKE_CURRENT_SOURCE_DIR}/runtest.py "^${DEMO_TEST}\\\$"
                                                        --client=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/demo)

        # Tell python tests where to look for pymbedtls*.so
        set_property(TEST ${INTEGRATION_TEST_PREFIX}${DEMO_TEST}
                     APPEND PROPERTY ENVIRONMENT "PYTHONPATH=${PYMBEDTLS_MODULE_DIR}")
        # Limit each test suite to 15 minutes
        set_property(TEST ${INTEGRATION_TEST_PREFIX}${DEMO_TEST} PROPERTY TIMEOUT 900)

        add_valgrind(${INTEGRATION_TEST_PREFIX}${DEMO_TEST})
    endif()
endforeach()

set(NSH_LWM2M_DIR "${CMAKE_CURRENT_SOURCE_DIR}/framework/nsh-lwm2m")

if(NOT MBEDTLS_LIBRARY)
    message(FATAL_ERROR "mbed TLS is required for integration tests")
endif()

set(_MISSING_PYTHON_MSG "If you have it installed on your system, but your cmake version is outdated you could try to restart the process that lead to this error message with: -DPython_ADDITIONAL_VERSIONS=3.x")

if(Python_ADDITIONAL_VERSIONS)
    find_package(PythonInterp)
else()
    find_package(PythonInterp 3)
endif()
if(NOT PYTHONINTERP_FOUND
        OR PYTHON_VERSION_MAJOR LESS 3
        OR (PYTHON_VERSION_MAJOR EQUAL 3 AND PYTHON_VERSION_MINOR LESS 5))
    message(FATAL_ERROR
            "Python >=3.5 is required for integration tests. ${_MISSING_PYTHON_MSG}")
endif()
find_package(PythonLibs "${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}")
if(NOT PYTHONLIBS_FOUND)
    message(FATAL_ERROR
            "libpython ${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR} is required for integration tests.")
endif()
unset(_MISSING_PYTHON_MSG)

add_custom_target(pymbedtls COMMAND
                  python3 -m pip install --target ${PYMBEDTLS_MODULE_DIR} "${NSH_LWM2M_DIR}/pymbedtls")

set(WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
configure_file("run_tests.sh.in" "run_tests.sh")
add_custom_target(integration_check_st
                  COMMAND ./run_tests.sh -s
                  DEPENDS demo pymbedtls)
add_custom_target(integration_check
                  COMMAND ./run_tests.sh
                  DEPENDS integration_check_st)
add_custom_target(integration_check_hsm
                  COMMAND ./run_tests.sh -h
                  DEPENDS demo pymbedtls)
add_dependencies(check integration_check)
