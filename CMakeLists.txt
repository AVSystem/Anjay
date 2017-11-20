# Copyright 2017 AVSystem <avsystem@avsystem.com>
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

cmake_minimum_required(VERSION 2.8.11)

option(WITH_FUZZ_TESTS "Compile fuzz tests using the AFL fuzzer" OFF)
if(WITH_FUZZ_TESTS)
    if(AFL_FUZZER_DIR)
        set(CMAKE_C_COMPILER "${AFL_FUZZER_DIR}/afl-gcc")
        set(AFL_FUZZ_EXECUTABLE "${AFL_FUZZER_DIR}/afl-fuzz")
    else()
        set(CMAKE_C_COMPILER "afl-gcc")
        set(AFL_FUZZ_EXECUTABLE "afl-fuzz")
    endif()
endif()

project(anjay C)
set(ANJAY_VERSION "1.5.0" CACHE STRING "Anjay library version")
set(ANJAY_BINARY_VERSION 1.0.0)

set(ANJAY_BUILD_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/lib")

# new policy in CMake 2.8.12 for link interface
if(NOT CMAKE_VERSION VERSION_LESS "2.8.12")
    cmake_policy(SET CMP0022 OLD)
endif()

set(CMAKE_USE_RELATIVE_PATHS TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/install_utils.cmake)

################# FEATURES #####################################################

include(CMakeDependentOption)

# compilation flags
if(NOT CMAKE_VERSION VERSION_LESS 3.1)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_EXTENSIONS OFF)
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
endif()

option(WITH_EXTRA_WARNINGS "Enable extra compilation warnings" OFF)
if(WITH_EXTRA_WARNINGS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Winit-self -Wmissing-declarations -Wc++-compat -Wsign-conversion -Wconversion -Wcast-qual -Wno-variadic-macros -Wno-long-long")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/PosixFeatures.cmake)

option(WITH_DEMO "Compile DEMO applications" ON)
option(WITH_LIBRARY_SHARED "Compile Anjay as shared library" ON)
cmake_dependent_option(WITH_STATIC_DEPS_LINKED
                       "Directly link shared library with its static dependencies such as avs_commons (e.g. for interpreted language bindings)"
                       OFF WITH_LIBRARY_SHARED OFF)

option(WITH_ACCESS_CONTROL "Enable core support for Access Control mechanism" ON)
option(WITH_BLOCK_RECEIVE "Enable support for receiving CoAP BLOCK transfers" ON)
option(WITH_BLOCK_SEND "Enable support for sending data in CoAP BLOCK mode" ON)
option(WITH_BOOTSTRAP "Enable LwM2M Bootstrap Interface support" ON)
option(WITH_DISCOVER "Enable support for LwM2M Discover operation" ON)
option(WITH_DOWNLOADER "Enable support for downloader API" ON)
option(WITH_OBSERVE "Enable support for Information Reporting interface (Observe)" ON)
if(WITH_OBSERVE)
    option(WITH_CON_ATTR
           "Enable support for a custom attribute that controls Confirmable notifications" OFF)
endif()
option(WITH_LEGACY_CONTENT_FORMAT_SUPPORT
       "Enable support for pre-LwM2M 1.0 CoAP Content-Format values (1541-1543)" OFF)
option(WITH_JSON "Enable support for JSON content format (output only)" OFF)

cmake_dependent_option(WITH_BLOCK_DOWNLOAD "Enable support for CoAP(S) downloads" ON WITH_DOWNLOADER OFF)
cmake_dependent_option(WITH_HTTP_DOWNLOAD "Enable support for HTTP(S) downloads" OFF WITH_DOWNLOADER OFF)

option(WITH_AVS_LOG "Enable logging support" ON)

cmake_dependent_option(AVS_LOG_WITH_TRACE "Enable TRACE level logging" OFF WITH_AVS_LOG OFF)
cmake_dependent_option(WITH_INTERNAL_LOGS "Enable logging from inside AVSystem Commons libraries" ON WITH_AVS_LOG OFF)
cmake_dependent_option(WITH_INTERNAL_TRACE "Enable TRACE-level logs inside AVSystem Commons libraries" ON AVS_LOG_WITH_TRACE OFF)

option(WITH_NET_STATS "Enable measuring amount of LwM2M traffic" ON)

# -fvisibility, #pragma GCC visibility
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/visibility.c
     "#pragma GCC visibility push(default)\nint f();\n#pragma GCC visibility push(hidden)\nint f() { return 0; }\n#pragma GCC visibility pop\nint main() { return f(); }\n\n")
try_compile(HAVE_VISIBILITY
            ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp
            ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/visibility.c
            COMPILE_DEFINITIONS -Wall -Wextra -Werror -fvisibility=default
            LINK_LIBRARIES -Wl,--exclude-libs,ALL)

################# TUNABLES #####################################################

set(MAX_PK_OR_IDENTITY_SIZE 2048 CACHE STRING
    "Maximum supported size (in bytes) of 'PK or Identity' Resource in Security object.")
set(MAX_SERVER_PK_OR_IDENTITY_SIZE 2048 CACHE STRING
    "Maximum supported size (in bytes) of 'Security PK or Identity' Resource in Security object.")
set(MAX_SECRET_KEY_SIZE 256 CACHE STRING
    "Maximum supported size (in bytes) of 'Secret Key' Resource in Security object.")

set(MAX_OBSERVABLE_RESOURCE_SIZE 2048 CACHE STRING
    "Maximum supported size (in bytes) of a single notification value.")

# Following options refer to the payload of plaintext-encoded CoAP packets.
set(MAX_FLOAT_STRING_SIZE 64 CACHE STRING
    "Maximum supported length (in characters) of a string that can be parsed as a single-precision float value, including trailing nullbyte.")
set(MAX_DOUBLE_STRING_SIZE 512 CACHE STRING
    "Maximum supported length (in characters) of a string that can be parsed as a double-precision float value, including trailing nullbyte.")

# CoAP guarantees that Uri-Path/Uri-Query/Location-Path/Location-Query option
# values are at most 255 characters long, so default values will work for all
# CoAP requests. Reducing these values is discouraged.
set(MAX_URI_SEGMENT_SIZE 256 CACHE STRING
    "Maximum supported length (in characters) of a single Uri-Path/Location-Path CoAP option value, including trailing nullbyte.")
set(MAX_URI_QUERY_SEGMENT_SIZE 256 CACHE STRING
    "Maximum supported length (in characters) of a single Uri-Query CoAP option value, including trailing nullbyte.")

################# CONVENIENCE SUPPORT ##########################################

macro(make_absolute_sources ABSVAR)
    set(${ABSVAR})
    foreach(SRC ${ARGN})
        set(${ABSVAR} ${${ABSVAR}} ${CMAKE_CURRENT_SOURCE_DIR}/${SRC})
    endforeach()
endmacro()

################# CODE #########################################################

set(CORE_SOURCES
    src/coap/id_source/auto.c
    src/coap/id_source/static.c
    src/coap/stream/client_internal.c
    src/coap/stream/common.c
    src/coap/stream/in.c
    src/coap/stream/out.c
    src/coap/stream/server_internal.c
    src/coap/stream/stream_internal.c
    src/interface/register.c
    src/io/base64_out.c
    src/io/dynamic.c
    src/io/opaque.c
    src/io/output_buf.c
    src/io/text.c
    src/io/tlv_in.c
    src/io/tlv_out.c
    src/dm_core.c
    src/dm/dm_attributes.c
    src/dm/dm_execute.c
    src/dm/dm_handlers.c
    src/dm/modules.c
    src/dm/query.c
    src/anjay_core.c
    src/io_core.c
    src/io_utils.c
    src/notify.c
    src/servers/activate.c
    src/servers/connection_info.c
    src/servers/offline.c
    src/servers/reload.c
    src/servers/register_internal.c
    src/servers/servers_internal.c
    src/raw_buffer.c
    src/sched.c
    src/utils_core.c)
if(WITH_ACCESS_CONTROL)
    set(CORE_SOURCES ${CORE_SOURCES} src/access_control_utils.c)
endif()
if(WITH_DOWNLOADER)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/downloader/downloader.c)
endif()
if(WITH_BLOCK_DOWNLOAD)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/downloader/coap.c)
endif()
if(WITH_HTTP_DOWNLOAD)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/downloader/http.c)
endif()
if(WITH_BLOCK_SEND)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/coap/block/response.c
        src/coap/block/request.c
        src/coap/block/transfer.c)
endif()
if(WITH_BOOTSTRAP)
    set(CORE_SOURCES ${CORE_SOURCES} src/interface/bootstrap_core.c)
endif()
if(WITH_DISCOVER)
    set(CORE_SOURCES ${CORE_SOURCES} src/dm/discover.c)
endif()
if(WITH_OBSERVE)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/observe_core.c
        src/observe_io.c)
endif()
if(WITH_JSON)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/io/json_out.c)
endif()
set(CORE_PRIVATE_HEADERS
    src/access_control_utils.h
    src/coap/block/request.h
    src/coap/block/response.h
    src/coap/block/transfer.h
    src/coap/block/transfer_impl.h
    src/coap/id_source/id_source.h
    src/coap/id_source/auto.h
    src/coap/id_source/static.h
    src/coap/coap_stream.h
    src/coap/stream/client_internal.h
    src/coap/stream/common.h
    src/coap/stream/in.h
    src/coap/stream/out.h
    src/coap/stream/server_internal.h
    src/coap/stream/stream_internal.h
    src/dm_core.h
    src/dm/dm_attributes.h
    src/dm/discover.h
    src/dm/dm_execute.h
    src/dm/query.h
    src/anjay_core.h
    src/interface/bootstrap_core.h
    src/interface/register.h
    src/io_core.h
    src/io/tlv.h
    src/io/vtable.h
    src/observe_core.h
    src/sched_internal.h
    src/servers.h
    src/servers/activate.h
    src/servers/connection_info.h
    src/servers/register_internal.h
    src/servers/servers_internal.h
    src/utils_core.h)
set(CORE_MODULES_HEADERS
    include_modules/anjay_modules/dm_utils.h
    include_modules/anjay_modules/dm/attributes.h
    include_modules/anjay_modules/dm/execute.h
    include_modules/anjay_modules/dm/modules.h
    include_modules/anjay_modules/downloader.h
    include_modules/anjay_modules/io_utils.h
    include_modules/anjay_modules/notify.h
    include_modules/anjay_modules/observe.h
    include_modules/anjay_modules/sched.h
    include_modules/anjay_modules/time_defs.h
    include_modules/anjay_modules/raw_buffer.h)
set(CORE_PUBLIC_HEADERS
    include_public/anjay/anjay.h
    include_public/anjay/core.h
    include_public/anjay/dm.h
    include_public/anjay/io.h)


set(ALL_SOURCES
    ${CORE_SOURCES}
    ${CORE_PRIVATE_HEADERS}
    ${CORE_MODULES_HEADERS}
    ${CORE_PUBLIC_HEADERS})
make_absolute_sources(ABSOLUTE_SOURCES ${ALL_SOURCES})

set(TEST_SOURCES
    ${ALL_SOURCES}
    src/coap/test/servers.c
    src/coap/test/servers.h
    src/coap/test/stream.c
    src/coap/test/block_response.c
    src/interface/test/bootstrap_mock.h
    src/io/test/bigdata.h
    src/test/observe_mock.h
    test/src/coap/stream.c
    test/src/coap/socket.c
    test/src/dm.c
    test/src/mock_clock.c
    test/src/mock_dm.c
    test/include/anjay_test/coap/stream.h
    test/include/anjay_test/dm.h
    test/include/anjay_test/mock_clock.h
    test/include/anjay_test/mock_dm.h)
make_absolute_sources(ABSOLUTE_TEST_SOURCES ${TEST_SOURCES})

################# MODULES ######################################################

set(SOURCES)
set(PRIVATE_HEADERS)
set(MODULES_HEADERS)
set(PUBLIC_HEADERS)
set(TEST_SOURCES)

macro(SUBDIRLIST result curdir)
  file(GLOB children RELATIVE ${curdir} ${curdir}/*)
  set(dirlist "")
  foreach(child ${children})
    if(IS_DIRECTORY ${curdir}/${child})
      list(APPEND dirlist ${child})
    endif()
  endforeach()
  set(${result} ${dirlist})
endmacro()

SUBDIRLIST(ALL_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/modules")

macro(DEFINE_MODULE MOD_NAME MOD_DEFAULT MOD_DESC)
    option(WITH_MODULE_${MOD_NAME} "${MOD_DESC}" ${MOD_DEFAULT})
    if(WITH_MODULE_${MOD_NAME})
        add_subdirectory(modules/${MOD_NAME})
        set(MODULES_INCLUDE_DIRS ${MODULES_INCLUDE_DIRS}
                                 ${CMAKE_CURRENT_SOURCE_DIR}/modules/${MOD_NAME}/include_modules)
        set(PUBLIC_INCLUDE_DIRS ${PUBLIC_INCLUDE_DIRS}
                                ${CMAKE_CURRENT_SOURCE_DIR}/modules/${MOD_NAME}/include_public)
    endif()
endmacro()

DEFINE_MODULE(persistence ON "Persistence support")
if(WITH_MODULE_persistence)
    DEFINE_MODULE(attr_storage ON "Automatic attribute storage module")
    if(WITH_ACCESS_CONTROL)
        DEFINE_MODULE(access_control ON "Access control object implementation module")
    endif()
endif()
DEFINE_MODULE(security ON "Security object module")
DEFINE_MODULE(server ON "Server object module")
if(WITH_DOWNLOADER OR WITH_BLOCK_RECEIVE)
    DEFINE_MODULE(fw_update ON "Firmware Update object module")
endif()

################# LIBRARIES ####################################################

set(DEPS_LIBRARIES "")
set(DEPS_LIBRARIES_WEAK "")

# avs_commons required components.
set(AVS_COMMONS_COMPONENTS algorithm coap list vector rbtree buffer net stream utils)
if(WITH_HTTP_DOWNLOAD)
    list(APPEND AVS_COMMONS_COMPONENTS http)
endif()

foreach(_component ${AVS_COMMONS_COMPONENTS})
    string(TOUPPER ${_component} _component_uppercase)

    # Enable compilation of each component.
    set(WITH_AVS_${_component_uppercase} ON CACHE INTERNAL "")

    # And mark it as a weak dependency.
    set(DEPS_LIBRARIES_WEAK ${DEPS_LIBRARIES_WEAK} avs_${_component})
endforeach()

option(WITH_LOCAL_AVS_COMMONS "Use locally installed avs_commons libraries" OFF)

if(WITH_LOCAL_AVS_COMMONS)
    find_package(avs_commons COMPONENTS ${AVS_COMMONS_COMPONENTS} REQUIRED)
else()
    option(WITH_AVS_UNIT "Enable compilation of avs_unit to allow unit testing" OFF)
    if(WITH_AVS_UNIT)
        find_program(VALGRIND_EXECUTABLE valgrind)
        cmake_dependent_option(WITH_VALGRIND "Enable usage of valgrind during unit tests" OFF "VALGRIND_EXECUTABLE" OFF)
    endif()

    set(_DTLS_BACKENDS "mbedtls" "openssl" "tinydtls")
    set(DTLS_BACKEND "mbedtls" CACHE STRING "DTLS backend to use; possible values: <empty> ${_DTLS_BACKENDS}")

    # Reset enabled backends first, to avoid issues with CMakeCache
    set(WITH_MBEDTLS OFF CACHE INTERNAL "")
    set(WITH_OPENSSL OFF CACHE INTERNAL "")
    set(WITH_TINYDTLS OFF CACHE INTERNAL "")

    string(TOLOWER "${DTLS_BACKEND}" _DTLS_BACKEND_LOWERCASE)
    if(_DTLS_BACKEND_LOWERCASE STREQUAL "mbedtls")
        set(WITH_MBEDTLS ON CACHE INTERNAL "")
    elseif(_DTLS_BACKEND_LOWERCASE STREQUAL "openssl")
        set(OPENSSL_CUSTOM_CIPHERS_ENABLED ON CACHE INTERNAL "")
        set(WITH_OPENSSL ON CACHE INTERNAL "")
        set(WITH_OPENSSL_CUSTOM_CIPHERS "ECDHE-ECDSA-AES128-CCM8:PSK-AES128-CCM8" CACHE INTERNAL "")
    elseif(_DTLS_BACKEND_LOWERCASE STREQUAL "tinydtls")
        set(WITH_TINYDTLS ON CACHE INTERNAL "")
    elseif(NOT _DTLS_BACKEND_LOWERCASE STREQUAL "")
        message(FATAL_ERROR "Unsupported DTLS backend: ${_DTLS_BACKEND_LOWERCASE}; possible values: ${_DTLS_BACKENDS}")
    endif()

    message(STATUS "DTLS backend: ${_DTLS_BACKEND_LOWERCASE}")

    add_subdirectory(avs_commons/git)
endif()

set(DEPS_INCLUDE_DIRS ${DEPS_INCLUDE_DIRS} ${avs_commons_INCLUDE_DIRS})

if(WITH_AVS_LOG)
    set(DEPS_LIBRARIES_WEAK ${DEPS_LIBRARIES_WEAK} avs_log)
endif()

################# PUBLIC/MODULE INCLUDE DIRS ###################################

set(PUBLIC_INCLUDE_DIRS ${PUBLIC_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/include_public")
set(MODULES_INCLUDE_DIRS ${MODULES_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/include_modules")

include_directories(SYSTEM ${DEPS_INCLUDE_DIRS})
include_directories(${PUBLIC_INCLUDE_DIRS}
                    ${MODULES_INCLUDE_DIRS})

################# LINK #########################################################

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config/config.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/config/config.h")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/config")

if(WITH_LIBRARY_SHARED)
    add_library(${PROJECT_NAME} SHARED ${ABSOLUTE_SOURCES})
    if(WITH_STATIC_DEPS_LINKED)
        target_link_libraries(${PROJECT_NAME} ${DEPS_LIBRARIES} ${DEPS_LIBRARIES_WEAK})
    else()
        target_link_libraries(${PROJECT_NAME} ${DEPS_LIBRARIES})
        target_link_libraries(${PROJECT_NAME} LINK_INTERFACE_LIBRARIES ${DEPS_LIBRARIES_WEAK})
        anjay_emit_deps(${PROJECT_NAME} ${DEPS_LIBRARIES_WEAK})
    endif()
    set_target_properties(${PROJECT_NAME} PROPERTIES
                          VERSION ${ANJAY_BINARY_VERSION})
    if(HAVE_VISIBILITY AND NOT WITH_STATIC_DEPS_LINKED)
        get_property(LINK_FLAGS TARGET ${PROJECT_NAME} PROPERTY LINK_FLAGS)
        set_property(TARGET ${PROJECT_NAME} PROPERTY LINK_FLAGS "${LINK_FLAGS} -Wl,--exclude-libs,ALL")
    endif()
    if(APPLE)
        get_property(LINK_FLAGS TARGET ${PROJECT_NAME} PROPERTY LINK_FLAGS)
        set_property(TARGET ${PROJECT_NAME} PROPERTY LINK_FLAGS "${LINK_FLAGS} -Wl,-undefined,dynamic_lookup")
    endif()
endif()

add_library(${PROJECT_NAME}_static STATIC ${ABSOLUTE_SOURCES})
target_link_libraries(${PROJECT_NAME}_static ${DEPS_LIBRARIES} ${DEPS_LIBRARIES_WEAK})
anjay_emit_deps(${PROJECT_NAME}_static ${DEPS_LIBRARIES} ${DEPS_LIBRARIES_WEAK})

################# DEMO #########################################################

if(WITH_DEMO)
    find_program(OPENSSL_EXECUTABLE openssl)
    if(OPENSSL_EXECUTABLE)
        if(NOT EXISTS "${ANJAY_BUILD_OUTPUT_DIR}/certs/client.crt.der")
            execute_process(COMMAND
                            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate-certs.sh"
                            "${ANJAY_BUILD_OUTPUT_DIR}/certs"
                            RESULT_VARIABLE RES)
            if(NOT ${RES} EQUAL 0)
                message(FATAL_ERROR "could not generate SSL certificates")
            endif()
        endif()
    elseif(WITH_AVS_UNIT)
        message(FATAL_ERROR "OpenSSL command line utility is required for unit tests")
    endif()

    set(ANJAY_INCLUDE_DIRS ${PUBLIC_INCLUDE_DIRS})
    set(ANJAY_LIBRARIES ${PROJECT_NAME})
    set(ANJAY_LIBRARIES_STATIC ${PROJECT_NAME}_static)
    add_subdirectory(demo)
endif()

################# TEST ########################################################

macro(add_anjay_test NAME)
    add_executable(${NAME}_test EXCLUDE_FROM_ALL ${ARGN})
    target_link_libraries(${NAME}_test avs_unit dl)
    set_property(TARGET ${NAME}_test APPEND PROPERTY COMPILE_DEFINITIONS
                 ANJAY_TEST
                 "ANJAY_BIN_DIR=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\"")
    set_property(TARGET ${NAME}_test APPEND PROPERTY COMPILE_FLAGS
                 "-Wno-pedantic -Wno-overlength-strings")

    if(VALGRIND)
        set(VALGRIND_LOG ${VALGRIND} --log-file=${ANJAY_BUILD_OUTPUT_DIR}/VALGRIND.${NAME}.log)
    else()
        set(VALGRIND_LOG)
    endif()

    add_test(NAME ${NAME}_test
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
             COMMAND ${VALGRIND_LOG} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${NAME}_test)

    add_custom_target(${NAME}_check COMMAND ${CMAKE_CTEST_COMMAND} -R "^${NAME}_test$$" -V DEPENDS ${NAME}_test)
    add_dependencies(anjay_unit_check ${NAME}_check)
endmacro()

enable_testing()

add_custom_target(check)
add_custom_target(anjay_unit_check)
add_dependencies(check anjay_unit_check)

if (TARGET avs_commons_check)
    add_dependencies(check avs_commons_check)
endif()

################# SOURCE VALIDATION ############################################

foreach(F ${ABSOLUTE_SOURCES})
    add_test(NAME test_${F}_visibility COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_visibility.py ${F})
    add_test(NAME test_${F}_headers COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_headers.py ${F})
endforeach()

add_custom_target(visibility_check COMMAND ${CMAKE_CTEST_COMMAND} -R "'^test_.*_visibility$$'")
add_dependencies(anjay_unit_check visibility_check)

add_custom_target(headers_check COMMAND ${CMAKE_CTEST_COMMAND} -R "'^test_.*_headers$$'")
add_dependencies(anjay_unit_check headers_check)

set(ABSOLUTE_HEADERS)
foreach(F ${ABSOLUTE_SOURCES})
    if(F MATCHES [.]h$)
        list(APPEND ABSOLUTE_HEADERS ${F})
    endif()
endforeach()
add_test(NAME test_function_duplicates COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_duplicates.py ${ABSOLUTE_HEADERS})
add_test(NAME test_markdown_toc COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/markdown-toc.py --check "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

add_custom_target(function_duplicates_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_function_duplicates$$'")

add_custom_target(toc_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_markdown_toc$$'")

add_dependencies(anjay_unit_check
                 function_duplicates_check
                 toc_check)

################# SYMBOL VALIDATION ############################################

add_custom_target(symbols_check COMMAND ${CMAKE_CTEST_COMMAND} -R "'^test_.*_symbols$$'" --output-on-failure)

if(WITH_LIBRARY_SHARED AND NOT WITH_STATIC_DEPS_LINKED)
    add_test(NAME test_so_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_symbols.sh $<TARGET_FILE:${PROJECT_NAME}> anjay_ ANJAY_)
    add_dependencies(symbols_check ${PROJECT_NAME})
endif()

add_test(NAME test_a_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_symbols.sh $<TARGET_FILE:${PROJECT_NAME}_static> anjay_ ANJAY_ _anjay_ _ANJAY_)
add_dependencies(symbols_check ${PROJECT_NAME}_static)

add_dependencies(anjay_unit_check symbols_check)

################# UNIT TESTS ###################################################

include(ProcessorCount)
ProcessorCount(ANJAY_DEFAULT_NPROC)

set(NPROC "${ANJAY_DEFAULT_NPROC}" CACHE STRING "Number of threads for multi-threaded build/test operations")

if(WITH_VALGRIND)
    set(VALGRIND ${VALGRIND_EXECUTABLE} --leak-check=full --track-origins=yes -q --error-exitcode=63 --suppressions=${CMAKE_CURRENT_SOURCE_DIR}/valgrind_test.supp)
endif()

if(WITH_AVS_UNIT)
    include_directories(test/include)
    add_anjay_test(${PROJECT_NAME} ${ABSOLUTE_TEST_SOURCES})
    target_link_libraries(${PROJECT_NAME}_test ${DEPS_LIBRARIES} ${DEPS_LIBRARIES_WEAK})
endif()

add_subdirectory(test/codegen)

cmake_dependent_option(WITH_INTEGRATION_TESTS "Enable integration tests" OFF WITH_DEMO OFF)
if(WITH_INTEGRATION_TESTS)
    add_subdirectory(test/integration)
endif()

################# FUZZ TESTING #################################################

if(WITH_FUZZ_TESTS)
    add_subdirectory(test/fuzz)

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
                                                           WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test/fuzz")
    endforeach()
endif()

################# DOCS #########################################################

add_custom_target(doc)

set(ANJAY_SPHINX_DOC_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx/source")
set(ANJAY_SPHINX_DOC_CONF_DIR "${ANJAY_BUILD_OUTPUT_DIR}/doc/sphinx")

option(WITH_DOC_CHECK "Enables/disables sphinx documentation validation" OFF)

if(EXISTS "${ANJAY_SPHINX_DOC_ROOT_DIR}/conf.py.in")
    find_program(SPHINX_BUILD_EXECUTABLE sphinx-build
                 HINTS $ENV{SPHINX_DIR} PATH_SUFFIXES bin)

    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sphinx.cmake)

    configure_file(${ANJAY_SPHINX_DOC_ROOT_DIR}/conf.py.in
                   ${ANJAY_SPHINX_DOC_CONF_DIR}/conf.py
                   @ONLY)
    add_custom_target(doc_sphinx
                      COMMAND ${SPHINX_BUILD_EXECUTABLE}
                              -b html
                              -c ${ANJAY_SPHINX_DOC_CONF_DIR}
                              ${ANJAY_SPHINX_DOC_ROOT_DIR}
                              ${ANJAY_SPHINX_DOC_CONF_DIR}/html
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx)

    add_dependencies(doc doc_sphinx)

    if(WITH_DOC_CHECK)
        if(NOT SPHINX_BUILD_EXECUTABLE)
            message(FATAL_ERROR "Sphinx not found (this can be suppressed by WITH_DOC_CHECK=OFF)")
        endif()
        # sphinx-based .. snippet-source:: validation
        add_test(NAME test_doc
                 COMMAND ${SPHINX_BUILD_EXECUTABLE}
                         -Q -b snippet_source_lint
                         -c ${ANJAY_SPHINX_DOC_CONF_DIR}
                         ${ANJAY_SPHINX_DOC_ROOT_DIR}
                         ${ANJAY_SPHINX_DOC_CONF_DIR}/lint
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx)
        set_property(TEST test_doc APPEND PROPERTY ENVIRONMENT
                     "ANJAY_SPHINX_DOC_CONF_DIR=${ANJAY_SPHINX_DOC_CONF_DIR}"
                     "ANJAY_SPHINX_DOC_ROOT_DIR=${ANJAY_SPHINX_DOC_ROOT_DIR}"
                     "CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                     "SNIPPET_SOURCE_MD5FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/test_examples.md5")

        add_custom_target(doc_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_doc$$'")
        add_dependencies(anjay_unit_check doc_check)
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
    find_package(Doxygen)

    set(DOXYGEN_INPUT_PATHS ${PUBLIC_INCLUDE_DIRS})
    # doxygen expects whitespace-separated list, cmake stores them as
    # semicolon-separated strings
    string(REPLACE ";" " " DOXYGEN_INPUT_PATHS "${DOXYGEN_INPUT_PATHS}")

    set(DOXYGEN_OUTPUT_DIR ${ANJAY_BUILD_OUTPUT_DIR}/doc/doxygen)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
                   ${ANJAY_BUILD_OUTPUT_DIR}/doc/doxygen/Doxyfile
                   @ONLY)

    add_custom_target(doc_doxygen
                      COMMAND ${DOXYGEN_EXECUTABLE} ${ANJAY_BUILD_OUTPUT_DIR}/doc/doxygen/Doxyfile
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})


    if(TARGET doc_sphinx)
        add_dependencies(doc_sphinx doc_doxygen)
    endif()
    add_dependencies(doc doc_doxygen)
endif()

################# STATIC ANALYSIS ##############################################

option(WITH_STATIC_ANALYSIS "Perform static analysis of the codebase on `make check`" OFF)
if(WITH_STATIC_ANALYSIS)
    find_program(SCAN_BUILD_BINARY scan-build)
    if(NOT SCAN_BUILD_BINARY)
        message(FATAL_ERROR "scan-build not found; specify path with -DSCAN_BUILD_BINARY or disable static analysis with -DWITH_STATIC_ANALYSIS=OFF")
    else()
        add_custom_target(analyze
                          COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/analyze"
                                  --output-dir "${CMAKE_CURRENT_BINARY_DIR}/static-analysis"
                                  --scan-build "${SCAN_BUILD_BINARY}")
        add_dependencies(check analyze)
    endif()
endif()

################# EXAMPLES #####################################################

set(ANJAY_EXAMPLES_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/examples/build")
set(ANJAY_TUTORIALS_ANJAY_BUILD_DIR "${ANJAY_EXAMPLES_BUILD_DIR}/anjay-build")
set(ANJAY_TUTORIALS_ANJAY_INSTALL_DIR "${ANJAY_EXAMPLES_BUILD_DIR}/anjay")

add_custom_target(examples)
add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_EXAMPLES_BUILD_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_TUTORIALS_ANJAY_BUILD_DIR}")

add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${CMAKE_CURRENT_SOURCE_DIR}
                        -B.
                        -DCMAKE_INSTALL_PREFIX="${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                        -DWITH_LIBRARY_SHARED=OFF
                        -DWITH_DEMO=OFF
                   COMMAND ${CMAKE_COMMAND} --build . --target install -- -j${NPROC}
                   WORKING_DIRECTORY ${ANJAY_TUTORIALS_ANJAY_BUILD_DIR})

add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${CMAKE_CURRENT_SOURCE_DIR}/examples/tutorial/
                        -B.
                        -DCMAKE_PREFIX_PATH="${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/examples"
                   COMMAND ${CMAKE_COMMAND} --build . -- -j${NPROC}
                   WORKING_DIRECTORY "${ANJAY_EXAMPLES_BUILD_DIR}")

add_dependencies(check examples)

################# INSTALL ######################################################

if(NOT LIB_INSTALL_DIR)
    set(LIB_INSTALL_DIR lib)
endif()

if(NOT INCLUDE_INSTALL_DIR)
    set(INCLUDE_INSTALL_DIR include)
endif()

# libraries
if(WITH_LIBRARY_SHARED)
    anjay_install_export(${PROJECT_NAME})
endif()
anjay_install_export(${PROJECT_NAME}_static)

# include directories
foreach(INCLUDE_DIR ${PUBLIC_INCLUDE_DIRS})
    install(DIRECTORY ${INCLUDE_DIR}/anjay
            DESTINATION ${INCLUDE_INSTALL_DIR}
            FILES_MATCHING REGEX "[.]h$")
endforeach()

# install CMake package
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/anjay-config.cmake.in
               ${ANJAY_BUILD_OUTPUT_DIR}/cmake/${PROJECT_NAME}-config.cmake
               @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/anjay-version.cmake.in
               ${ANJAY_BUILD_OUTPUT_DIR}/cmake/${PROJECT_NAME}-version.cmake
               @ONLY)

install(EXPORT ${PROJECT_NAME}-targets DESTINATION ${LIB_INSTALL_DIR}/${PROJECT_NAME})
install(FILES
        ${ANJAY_BUILD_OUTPUT_DIR}/cmake/${PROJECT_NAME}-config.cmake
        ${ANJAY_BUILD_OUTPUT_DIR}/cmake/${PROJECT_NAME}-version.cmake
        DESTINATION ${LIB_INSTALL_DIR}/${PROJECT_NAME})
