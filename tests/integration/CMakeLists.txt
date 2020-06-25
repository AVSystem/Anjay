# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
                  python3 "${NSH_LWM2M_DIR}/pymbedtls/setup.py" install --install-lib ${PYMBEDTLS_MODULE_DIR})

add_custom_target(integration_check_st
                  COMMAND ${CMAKE_CTEST_COMMAND} -R sensitive --output-on-failure
                  DEPENDS demo pymbedtls)
add_custom_target(integration_check
                  COMMAND ${CMAKE_CTEST_COMMAND} -E sensitive -j ${NPROC} ||
                          ${CMAKE_CTEST_COMMAND} -E sensitive --rerun-failed --output-on-failure
                  DEPENDS integration_check_st)
add_dependencies(check integration_check)
