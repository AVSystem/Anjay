# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

set(CODEGEN "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/anjay_codegen.py")
set(CODEGEN_TEST_INPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/input")
set(CODEGEN_TEST_PREFIX "test_codegen_")

file(GLOB_RECURSE CODEGEN_INPUTS RELATIVE ${CODEGEN_TEST_INPUT_ROOT} "${CODEGEN_TEST_INPUT_ROOT}/*.xml")

set(CODEGEN_CHECK_TARGETS)

foreach(CODEGEN_INPUT ${CODEGEN_INPUTS})
    string(REGEX REPLACE "^${CMAKE_CURRENT_SOURCE_DIR}" "" CODEGEN_TEST "${CODEGEN_INPUT}")
    string(REGEX REPLACE "\\.xml" "" CODEGEN_TEST "${CODEGEN_TEST}")
    string(REGEX REPLACE "/" "." CODEGEN_TEST "${CODEGEN_TEST}")

    set(INPUT "${CODEGEN_TEST_INPUT_ROOT}/${CODEGEN_INPUT}")
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${CODEGEN_TEST}.c")
    add_custom_command(OUTPUT "${OUTPUT}"
                       COMMAND "${CODEGEN}" -i "${INPUT}" -o "${OUTPUT}"
                       DEPENDS "${CODEGEN}" "${INPUT}" )
    list(APPEND CODEGEN_SOURCES "${OUTPUT}")
endforeach()

add_library(codegen_check OBJECT EXCLUDE_FROM_ALL ${CODEGEN_SOURCES})
set_target_properties(codegen_check PROPERTIES
                      COMPILE_FLAGS "-Wno-missing-declarations -Wno-unused-variable -Wno-unused-parameter")

add_dependencies(check codegen_check)
