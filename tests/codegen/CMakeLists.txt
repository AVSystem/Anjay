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

set(CODEGEN "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/anjay_codegen.py")
set(CODEGEN_TEST_INPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/input")
set(CODEGEN_TEST_PREFIX "test_codegen_")

file(GLOB_RECURSE CODEGEN_INPUTS RELATIVE ${CODEGEN_TEST_INPUT_ROOT} "${CODEGEN_TEST_INPUT_ROOT}/*.xml")

set(CODEGEN_CHECK_TARGETS)

enable_language(CXX)
string(REPLACE "-Wc++-compat" "" CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
string(REPLACE "-Wjump-misses-init" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "-std=[a-z0-9]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

foreach(CODEGEN_INPUT ${CODEGEN_INPUTS})
    string(REGEX REPLACE "^${CMAKE_CURRENT_SOURCE_DIR}" "" CODEGEN_TEST "${CODEGEN_INPUT}")
    string(REGEX REPLACE "\\.xml" "" CODEGEN_TEST "${CODEGEN_TEST}")
    string(REGEX REPLACE "/" "." CODEGEN_TEST "${CODEGEN_TEST}")

    set(INPUT "${CODEGEN_TEST_INPUT_ROOT}/${CODEGEN_INPUT}")
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${CODEGEN_TEST}.c")
    set(OUTPUT_CXX "${CMAKE_CURRENT_BINARY_DIR}/${CODEGEN_TEST}.cpp")
    add_custom_command(OUTPUT "${OUTPUT}"
                       COMMAND "${CODEGEN}" -i "${INPUT}" -o "${OUTPUT}"
                       DEPENDS "${CODEGEN}" "${INPUT}")
    add_custom_command(OUTPUT "${OUTPUT_CXX}"
                       COMMAND "${CODEGEN}" -x -i "${INPUT}" -o "${OUTPUT_CXX}"
                       DEPENDS "${CODEGEN}" "${INPUT}")
    list(APPEND CODEGEN_SOURCES "${OUTPUT}")
    list(APPEND CODEGEN_CXX_SOURCES "${OUTPUT_CXX}")
endforeach()

add_library(codegen_check OBJECT EXCLUDE_FROM_ALL ${CODEGEN_SOURCES})
set_target_properties(codegen_check PROPERTIES
                      COMPILE_FLAGS "-Wno-missing-declarations -Wno-unused-variable -Wno-unused-parameter")
target_include_directories(codegen_check PRIVATE
                           $<TARGET_PROPERTY:anjay,INTERFACE_INCLUDE_DIRECTORIES>)

add_library(codegen_check_cxx OBJECT EXCLUDE_FROM_ALL ${CODEGEN_CXX_SOURCES})
set_target_properties(codegen_check_cxx PROPERTIES
                      COMPILE_FLAGS "-Wno-missing-declarations -Wno-unused-variable -Wno-unused-parameter")
target_include_directories(codegen_check_cxx PRIVATE
                           $<TARGET_PROPERTY:anjay,INTERFACE_INCLUDE_DIRECTORIES>)

add_custom_target(codegen_check_with_object_registry COMMAND
                  "${CMAKE_CURRENT_SOURCE_DIR}/check_with_object_registry.sh" 0 1 2 3 4 5 17 18 22)
add_custom_target(codegen_check_cxx_with_object_registry COMMAND
                  "${CMAKE_CURRENT_SOURCE_DIR}/check_with_object_registry.sh" "--c++" 0 1 2 3 4 5 17 18 22)
add_dependencies(codegen_check codegen_check_with_object_registry)
add_dependencies(codegen_check_cxx codegen_check_cxx_with_object_registry)

add_dependencies(check codegen_check codegen_check_cxx)
