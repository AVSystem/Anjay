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
#
# Every header must be self-sufficient, i.e. one must be able to include it
# without having to also include anything else before.
#
# This function generates targets that for every passed header file generate
# an empty translation unit that includes just that header (and, optionally,
# some extra config headers before it). If that generated file compiles,
# that means all symbols it depends on are correctly included.
#
# Note that such file does not necessarily need to link; only compilation is
# checked.
#
# Arguments:
# - TARGET <name>                 - name of the custom target defined by the
#                                   function. All generated per-file targets
#                                   are set as dependencies of this one
# - TARGET_PREFIX <prefix>        - string prepended to all generated per-file
#                                   targets
# - FILES <path>...               - list of files to generate check targets for
# - DIRECTORIES <path>...         - list of directories to search for header
#                                   files (*.h) to generate checks for.
# - EXCLUDE_PATTERNS <regex>...   - list of regexes that will be matched
#                                   against file paths to determine if a
#                                   particular header file should be excluded
#                                   from checks.
# - INCLUDES <path>...            - list of files that need to be included
#                                   before any header file (e.g. avs_coap_config.h)
# - INCLUDE_DIRECTORIES <path>... - list of additional include directories to
#                                   add when compiling
# - COMPILE_OPTIONS <opt>...      - list of extra compilation options
# - LIBS <target>...              - list of targets to retrieve include
#                                   directories from
function(add_header_self_sufficiency_tests)
    set(options)
    set(one_value_args TARGET_PREFIX TARGET)
    set(multi_value_args FILES DIRECTORIES INCLUDES EXCLUDE_PATTERNS INCLUDE_DIRECTORIES COMPILE_OPTIONS LIBS)
    cmake_parse_arguments(SST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    add_custom_target(${SST_TARGET})

    set(includes)
    foreach(inc IN LISTS SST_INCLUDES)
        string(APPEND includes "#include <${inc}>\n")
    endforeach()

    set(headers ${SST_FILES})
    foreach(dir IN LISTS SST_DIRECTORIES)
        file(GLOB_RECURSE dir_headers "${dir}/*.h")
        list(APPEND headers ${dir_headers})
    endforeach()

    foreach(header IN LISTS headers)
        set(is_excluded FALSE)
        foreach(exclude_regex IN LISTS SST_EXCLUDE_PATTERNS)
            if("${header}" MATCHES "${exclude_regex}")
                message(STATUS "add_header_self_sufficiency_tests: excluding ${header} (match: ${exclude_regex})")
                set(is_excluded TRUE)
                break()
            endif()
        endforeach()

        if(is_excluded)
            continue()
        endif()

        string(REGEX REPLACE "[^a-zA-Z0-9]" _ target_name "${header}")
        set(input "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/${target_name}.c")
        set(target_name ${SST_PREFIX}_header_self_sufficiency_test_${target_name})

        file(GENERATE OUTPUT "${input}"
             CONTENT "${includes}\n#include <${header}>\nint main() { return 0; }\n")
        add_library(${target_name} OBJECT EXCLUDE_FROM_ALL "${input}")
        foreach(lib IN LISTS SST_LIBS)
            target_include_directories(${target_name} PRIVATE
                                       $<TARGET_PROPERTY:${lib},INTERFACE_INCLUDE_DIRECTORIES>)
        endforeach()
        target_include_directories(${target_name} PRIVATE ${SST_INCLUDE_DIRECTORIES})

        # make sure to fail compilation on any implicit dependency, including
        # functions used without forward-declarations
        target_compile_options(${target_name} PRIVATE
                               ${SST_COMPILE_OPTIONS}
                               -Werror=implicit)

        add_test(NAME ${target_name}
                 COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target_name})
        add_dependencies(${SST_TARGET} ${target_name})
    endforeach()
endfunction()
