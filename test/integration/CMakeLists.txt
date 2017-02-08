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
        add_valgrind(${INTEGRATION_TEST_PREFIX}${DEMO_TEST})
    endif()
endforeach()

set(NSH_LWM2M_DIR "${CMAKE_CURRENT_SOURCE_DIR}/framework/nsh-lwm2m")

if(NOT MBEDTLS_LIB_MBEDTLS)
    message(FATAL_ERROR "mbed TLS is required for integration tests")
endif()
find_package(PythonInterp 3)
if(NOT PYTHONINTERP_FOUND OR PYTHON_MINOR_VERSION LESS 5)
    message(FATAL_ERROR "Python >=3.5 is required for integration tests")
endif()
find_package(PythonLibs "${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}")
if(NOT PYTHONLIBS_FOUND)
    message(FATAL_ERROR "libpython >=3.5 is required for integration tests")
endif()
find_library(BOOST_PYTHON3_LIBRARY
             NAMES boost_python3 "boost_python-py${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR}")
if(NOT BOOST_PYTHON3_LIBRARY)
    message(FATAL_ERROR "boost_python for Python 3 is required for integration tests")
endif()

add_custom_target(pymbedtls COMMAND python3 "${NSH_LWM2M_DIR}/pymbedtls/setup.py" install --install-lib "${NSH_LWM2M_DIR}/lwm2m/coap")

execute_process(COMMAND nproc OUTPUT_VARIABLE NPROC OUTPUT_STRIP_TRAILING_WHITESPACE)

add_custom_target(integration_check
                  COMMAND ${CMAKE_CTEST_COMMAND} -j ${NPROC} --output-on-failure
                  DEPENDS demo pymbedtls)
add_dependencies(check integration_check)
