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

cmake_minimum_required(VERSION 3.4.0)
project(avs_coap C)

include(CMakeDependentOption)

set(DEFAULT_AVS_COAP_VERSION "")
find_package(Git)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always
                    OUTPUT_VARIABLE DEFAULT_AVS_COAP_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
endif()
if(DEFAULT_AVS_COAP_VERSION STREQUAL "")
    set(DEFAULT_AVS_COAP_VERSION "1.0-SNAPSHOT")
endif()

set(AVS_COAP_VERSION "${DEFAULT_AVS_COAP_VERSION}" CACHE STRING "avs_coap library version")

### library options

option(WITH_TEST "Compile unit tests" OFF)
option(WITH_POISONING "Poison libc symbols that shall not be used" OFF)
option(WITH_AVS_COAP_DIAGNOSTIC_MESSAGES "Include diagnostic payload in Abort messages" ON)
option(WITH_AVS_COAP_UDP "Enable CoAP over UDP support" ON)
option(WITH_AVS_COAP_STREAMING_API "Enable streaming API" ON)
option(WITH_AVS_COAP_OBSERVE "Enable support for observations" ON)
cmake_dependent_option(WITH_AVS_COAP_OBSERVE_PERSISTENCE "Enable observations persistence" ON "WITH_AVS_COAP_OBSERVE" OFF)
option(WITH_AVS_COAP_BLOCK "Enable support for BLOCK/BERT transfers" ON)

option(WITH_AVS_COAP_LOGS "Enable logging" ON)
cmake_dependent_option(WITH_AVS_COAP_TRACE_LOGS "Enable TRACE-level logging" ON "WITH_AVS_COAP_LOGS" OFF)

set(COAP_UDP_NOTIFY_CACHE_SIZE 4 CACHE STRING "Maximum number of notification tokens stored to match Reset responses to")

### depedencies

set(AVS_COMMONS_REQUIRED_COMPONENTS avs_buffer avs_compat_threading avs_list avs_net avs_sched avs_stream avs_utils avs_persistence avs_rbtree)

if(WITH_AVS_COAP_LOGS)
    set(WITH_AVS_LOG ON)
    list(APPEND AVS_COMMONS_REQUIRED_COMPONENTS avs_log)
endif()


if(WITH_TEST)
    set(WITH_AVS_UNIT ON)
    list(APPEND AVS_COMMONS_REQUIRED_COMPONENTS avs_unit)
endif()

### feature support

# -fvisibility, #pragma GCC visibility
if(NOT DEFINED HAVE_VISIBILITY)
    file(WRITE ${CMAKE_BINARY_DIR}/CMakeFiles/CMakeTmp/visibility.c "#pragma GCC visibility push(default)\nint f();\n#pragma GCC visibility push(hidden)\nint f() { return 0; }\n#pragma GCC visibility pop\nint main() { return f(); }\n\n")
    try_compile(HAVE_VISIBILITY
                ${CMAKE_BINARY_DIR}/CMakeFiles/CMakeTmp
                ${CMAKE_BINARY_DIR}/CMakeFiles/CMakeTmp/visibility.c
                COMPILE_DEFINITIONS -Wall -Wextra -Werror -fvisibility=default)
endif()

configure_file(src/avs_coap_config.h.in
               src/avs_coap_config.h)

configure_file(include_public/avsystem/coap/config.h.in
               generated/include_public/avsystem/coap/config.h)

### targets

set(PUBLIC_INCLUDES
    include_public/avsystem/coap/streaming.h
    include_public/avsystem/coap/async.h
    include_public/avsystem/coap/coap.h
    include_public/avsystem/coap/async_client.h
    include_public/avsystem/coap/code.h
    include_public/avsystem/coap/option.h
    include_public/avsystem/coap/token.h
    include_public/avsystem/coap/async_exchange.h
    include_public/avsystem/coap/udp.h
    include_public/avsystem/coap/writer.h
    include_public/avsystem/coap/async_server.h
    include_public/avsystem/coap/ctx.h
    include_public/avsystem/coap/observe.h
    ${CMAKE_CURRENT_BINARY_DIR}/generated/include_public/avsystem/coap/config.h)

set(SOURCES
    ${PUBLIC_INCLUDES}

    src/async/async_client.c
    src/async/async_client.h
    src/async/async_server.c
    src/async/exchange.c
    src/async/exchange.h
    src/code_utils.c
    src/code_utils.h
    src/common_utils.c
    src/common_utils.h
    src/ctx.c
    src/ctx.h
    src/ctx_vtable.h
    src/parse_utils.h

    src/options/iterator.c
    src/options/iterator.h
    src/options/option.c
    src/options/options.c
    src/options/option.h)

if(WITH_AVS_COAP_UDP)
    set(SOURCES ${SOURCES}
        src/udp/udp_ctx.c
        src/udp/udp_msg.h
        src/udp/udp_msg.c
        src/udp/udp_msg_cache.c
        src/udp/udp_msg_cache.h
        src/udp/udp_header.h
        src/udp/udp_tx_params.c
        src/udp/udp_tx_params.h)
endif()


if(WITH_AVS_COAP_STREAMING_API)
    set(SOURCES ${SOURCES}
        src/streaming/streaming_client.c
        src/streaming/streaming_client.h
        src/streaming/streaming_server.c
        src/streaming/streaming_server.h)
endif()

if(WITH_AVS_COAP_OBSERVE)
    set(SOURCES ${SOURCES}
        src/observe.c
        src/observe.h)
endif()

# avs_coap_library(NAME name
#                  [ PRIVATE_FLAGS flags... ])
function(add_coap_library)
    cmake_parse_arguments(add_coap_library "" "NAME" "PRIVATE_DEFINITIONS" ${ARGN})
    if (NOT add_coap_library_NAME)
        message(FATAL_ERROR "NAME is not specified")
    endif()

    set(name ${add_coap_library_NAME})
    set(private_definitions ${add_coap_library_PRIVATE_DEFINITIONS})
    add_library(${name} STATIC ${SOURCES})

    target_include_directories(${name} PRIVATE src)
    target_include_directories(${name} PUBLIC
                               $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include_public>
                               $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include_public>
                               $<INSTALL_INTERFACE:include>)
    target_include_directories(${name} PRIVATE
                               $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src>)
    target_link_libraries(${name} PUBLIC ${AVS_COMMONS_REQUIRED_COMPONENTS})
    target_compile_definitions(${name} PRIVATE ${private_definitions})
endfunction()

add_coap_library(NAME avs_coap)

### tests

if(WITH_TEST)
    enable_testing()

    add_coap_library(NAME avs_coap_for_tests
                     PRIVATE_DEFINITIONS AVS_UNIT_TESTING)
    set_target_properties(avs_coap_for_tests PROPERTIES EXCLUDE_FROM_ALL TRUE)

    set(TEST_SOURCES
        src/test/socket.c
        src/test/socket.h
        src/test/mock_clock.c
        src/test/mock_clock.h
        src/test/utils.h
        src/test/utils.c

        src/options/test/option.c
        src/options/test/options.c)

    if(WITH_AVS_COAP_UDP)
        set(TEST_SOURCES ${TEST_SOURCES}
            src/udp/test/async_client_with_big_data.c
            src/udp/test/async_client.c
            src/udp/test/async_server.c
            src/udp/test/big_data.h
            src/udp/test/fuzzer_cases.c
            src/udp/test/msg_cache.c
            src/udp/test/msg.c
            src/udp/test/udp_tx_params.c
            src/udp/test/utils.h
            src/udp/test/setsock.c)

        if(WITH_AVS_COAP_OBSERVE)
            set(TEST_SOURCES ${TEST_SOURCES}
                src/udp/test/async_observe.c)
        endif()

        if(WITH_AVS_COAP_STREAMING_API)
            set(TEST_SOURCES ${TEST_SOURCES}
                src/udp/test/streaming_client.c
                src/udp/test/streaming_server.c)
        endif()

        if(WITH_AVS_COAP_OBSERVE AND WITH_AVS_COAP_STREAMING_API)
        set(TEST_SOURCES ${TEST_SOURCES}
            src/udp/test/streaming_observe.c)
        endif()
    endif()


    add_executable(avs_coap_test EXCLUDE_FROM_ALL ${TEST_SOURCES})
    target_include_directories(avs_coap_test PRIVATE src)
    target_include_directories(avs_coap_test PRIVATE
                               $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src>)
    # dl required for mock_clock
    target_link_libraries(avs_coap_test PRIVATE avs_coap_for_tests avs_unit dl)
    target_compile_options(avs_coap_test PRIVATE
                           -Wno-c++-compat -Wno-missing-field-initializers)
    # disable identifier poisoning for tests
    target_compile_definitions(avs_coap_test PRIVATE AVS_UNIT_TESTING)

    find_program(VALGRIND_EXECUTABLE valgrind)
    if(VALGRIND_EXECUTABLE)
        # This is enabled in devconfig only, and should be disabled in CMakeLists.txt,
        # to avoid cross-compilation errors on stations with valgrind installed.
        option(WITH_VALGRIND "Enable usage of valgrind during unit tests" ON)
        set(VALGRIND ${VALGRIND_EXECUTABLE} --leak-check=full --track-origins=yes -q --error-exitcode=63 --log-file=VALGRIND.avs_coap_test.log)
    endif()
    if(VALGRIND AND WITH_VALGRIND)
        set(VALGRIND_CMD ${VALGRIND})
    else()
        set(VALGRIND_CMD)
    endif()

    add_test(NAME avs_coap_test COMMAND ${VALGRIND_CMD} $<TARGET_FILE:avs_coap_test>)

    add_custom_target(avs_coap_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "^avs_coap_test$" DEPENDS avs_coap_test)
    if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
        # only add "check" target if building avs_coap standalone
        add_custom_target(check)
        add_dependencies(check avs_coap_check)
    endif()

    add_subdirectory(test/fuzz)

    include(cmake/AddHeaderSelfSufficiencyTests.cmake)
    add_header_self_sufficiency_tests(TARGET avs_coap_public_header_self_sufficiency_check
                                      TARGET_PREFIX avs_coap
                                      DIRECTORIES include_public
                                      LIBS avs_coap_for_tests)
    add_header_self_sufficiency_tests(TARGET avs_coap_internal_header_self_sufficiency_check
                                      TARGET_PREFIX avs_coap
                                      INCLUDES
                                      "avs_coap_config.h"
                                      "x_log_config.h"
                                      INCLUDE_DIRECTORIES
                                      "${CMAKE_CURRENT_BINARY_DIR}/src"
                                      "${CMAKE_CURRENT_SOURCE_DIR}/src"
                                      COMPILE_OPTIONS
                                      # disable test-specific warnings
                                      $<TARGET_PROPERTY:avs_coap_test,COMPILE_OPTIONS>
                                      DIRECTORIES src
                                      EXCLUDE_PATTERNS ".*/test/.*"
                                      LIBS avs_coap_for_tests avs_unit)
    add_custom_target(avs_coap_header_self_sufficiency_check)
    add_dependencies(avs_coap_header_self_sufficiency_check
                     avs_coap_public_header_self_sufficiency_check
                     avs_coap_internal_header_self_sufficiency_check)
    add_dependencies(avs_coap_check avs_coap_header_self_sufficiency_check)
    function(add_cpp_header_warnings_check)
        set(options)
        set(one_value_args TARGET)
        set(multi_value_args INCLUDES)
        cmake_parse_arguments(HWC "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

        set(all_includes)
        foreach(include IN LISTS HWC_INCLUDES)
            string(REGEX REPLACE "^include_public/" "" include "${include}")
            list(APPEND all_includes "#include <${include}>")
        endforeach()
        string(REPLACE "#" "\\#" all_includes "${all_includes}")
        string(REPLACE "<" "\\<" all_includes "${all_includes}")
        string(REPLACE ">" "\\>" all_includes "${all_includes}")
        string(REPLACE "\\;" "\\n" all_includes "${all_includes}")

        set(source_file "${CMAKE_CURRENT_BINARY_DIR}/ensure-no-warnings-from-headers-if-cpp.cpp")
        add_custom_command(OUTPUT "${source_file}"
                           COMMAND /bin/echo > "${source_file}"
                           COMMAND for F in ${all_includes} \\; do /bin/echo "$$F" >> "${source_file}" \\; done)
        add_library("${HWC_TARGET}" OBJECT "${source_file}")
        target_include_directories("${HWC_TARGET}" PRIVATE $<TARGET_PROPERTY:avs_coap,INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_options("${HWC_TARGET}" PRIVATE -std=c++11 -Wall -Wextra -pedantic -Werror)
        set_target_properties("${HWC_TARGET}" PROPERTIES EXCLUDE_FROM_ALL TRUE)
        add_dependencies(avs_coap_check "${HWC_TARGET}")
    endfunction()

    enable_language(CXX)
    add_cpp_header_warnings_check(TARGET avs_coap_cpp_header_check
                                  INCLUDES ${PUBLIC_INCLUDES})
endif()


### distribution

install(TARGETS avs_coap EXPORT avs_coap-targets DESTINATION lib)
install(EXPORT avs_coap-targets DESTINATION lib/avs_coap)
install(DIRECTORY include_public/
        DESTINATION include
        FILES_MATCHING REGEX "[.]h$")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/include_public/avsystem/coap/config.h"
        DESTINATION include/avsystem/coap)

# see https://cmake.org/cmake/help/v3.4/module/CMakePackageConfigHelpers.html#example-generating-package-files
include(CMakePackageConfigHelpers)

configure_file(cmake/avs_coap-config.cmake.in
               cmake/avs_coap-config.cmake)
configure_package_config_file(cmake/avs_coap-config.cmake.in
                              ${CMAKE_CURRENT_BINARY_DIR}/avs_coap-config.cmake
                              INSTALL_DESTINATION lib/avs_coap)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/avs_coap-version.cmake
                                 VERSION "${AVS_COAP_VERSION}"
                                 COMPATIBILITY SameMajorVersion)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/avs_coap-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/avs_coap-version.cmake"
        DESTINATION lib/avs_coap)

install(SCRIPT cmake/fill-placeholders.cmake)
