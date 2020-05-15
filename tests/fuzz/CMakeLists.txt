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

option(WITH_FUZZ_TESTS "Compile fuzz tests using the AFL fuzzer" OFF)
if(NOT WITH_FUZZ_TESTS)
    return()
endif()

if(NOT "${CMAKE_C_COMPILER}" MATCHES "^(.*/)?afl-gcc$")
    message(WARNING "AFL fuzz tests require compiling with afl-gcc, but CMAKE_C_COMPILER is set to ${CMAKE_C_COMPILER}")
endif()

if(AFL_FUZZER_DIR)
    set(AFL_FUZZ_EXECUTABLE "${AFL_FUZZER_DIR}/afl-fuzz")
else()
    set(AFL_FUZZ_EXECUTABLE "afl-fuzz")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

set(FUZZ_TEST_NAMES "")
set(FUZZ_TEST_ARG_LISTS "")
set(FUZZ_TEST_CASE_DIRS "")
set(FUZZ_TEST_FINDINGS_DIRS "")

macro(add_fuzz_target NAME EXECUTABLE)
    get_filename_component(EXECUTABLE "${EXECUTABLE}" ABSOLUTE)

    list(APPEND FUZZ_TEST_NAMES ${NAME})
    list(APPEND FUZZ_TEST_ARG_LISTS "FUZZ_TEST_ARG_LIST_${NAME}")
    list(APPEND FUZZ_TEST_CASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/test_cases/${NAME}")
    list(APPEND FUZZ_TEST_FINDINGS_DIRS "${CMAKE_CURRENT_BINARY_DIR}/findings/${NAME}")

    set("FUZZ_TEST_ARG_LIST_${NAME}" "${EXECUTABLE}" ${ARGN})

    set(FUZZ_TEST_NAMES "${FUZZ_TEST_NAMES}")
    set(FUZZ_TEST_ARG_LISTS "${FUZZ_TEST_ARG_LISTS}")
    set(FUZZ_TEST_CASE_DIRS "${FUZZ_TEST_CASE_DIRS}")
    set(FUZZ_TEST_FINDINGS_DIRS "${FUZZ_TEST_FINDINGS_DIRS}")
endmacro()

file(GLOB_RECURSE FUZZ_TEST_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.c)

foreach(FUZZ_TEST_SOURCE ${FUZZ_TEST_SOURCES})
    get_filename_component(FUZZ_TEST_DIR "${FUZZ_TEST_SOURCE}" DIRECTORY)
    get_filename_component(FUZZ_TEST_OUTPUT "${FUZZ_TEST_SOURCE}" NAME_WE)
    set(FUZZ_TEST_OUTPUT "${FUZZ_TEST_DIR}/${FUZZ_TEST_OUTPUT}")
    string(REPLACE / _ FUZZ_TEST_NAME ${FUZZ_TEST_OUTPUT})

    add_executable(${FUZZ_TEST_NAME}_fuzz_test ${FUZZ_TEST_SOURCE} ${ABSOLUTE_SOURCES})
    target_link_libraries(${FUZZ_TEST_NAME}_fuzz_test PRIVATE ${PROJECT_NAME})
    target_include_directories(${FUZZ_TEST_NAME}_fuzz_test PRIVATE
                               "${ANJAY_SOURCE_DIR}"
                               $<TARGET_PROPERTY:anjay,INCLUDE_DIRECTORIES>)
    set_property(TARGET ${FUZZ_TEST_NAME}_fuzz_test APPEND PROPERTY COMPILE_DEFINITIONS ANJAY_FUZZ_TEST)

    add_fuzz_target(${FUZZ_TEST_NAME} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${FUZZ_TEST_NAME}_fuzz_test")
endforeach()

set(AFL_FUZZ_TIMEOUT_MS 1000)

list(LENGTH FUZZ_TEST_NAMES FUZZ_TEST_COUNT)
math(EXPR FUZZ_TEST_MAX "${FUZZ_TEST_COUNT} - 1")

foreach(i RANGE ${FUZZ_TEST_MAX})
    list(GET FUZZ_TEST_NAMES ${i} FUZZ_TEST_NAME)
    list(GET FUZZ_TEST_ARG_LISTS ${i} FUZZ_TEST_ARG_LIST_NAME)
    list(GET FUZZ_TEST_CASE_DIRS ${i} FUZZ_TEST_CASE_DIR)
    list(GET FUZZ_TEST_FINDINGS_DIRS ${i} FUZZ_TEST_FINDINGS_DIR)

    message(STATUS "Adding fuzz test target: fuzz_${FUZZ_TEST_NAME}: ${${FUZZ_TEST_ARG_LIST_NAME}}")
    add_custom_target(fuzz_${FUZZ_TEST_NAME}
                      COMMAND mkdir -p "${FUZZ_TEST_FINDINGS_DIR}"
                      COMMAND "${AFL_FUZZ_EXECUTABLE}" -i "${FUZZ_TEST_CASE_DIR}"
                                                       -o "${FUZZ_TEST_FINDINGS_DIR}"
                                                       -t ${AFL_FUZZ_TIMEOUT_MS}
                                                       "${${FUZZ_TEST_ARG_LIST_NAME}}"
                                                       WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                      DEPENDS ${FUZZ_TEST_NAME}_fuzz_test)
endforeach()
