# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

cmake_minimum_required(VERSION 2.8.12)

project(anjay C)
set(ANJAY_VERSION "1.15.3" CACHE STRING "Anjay library version")
set(ANJAY_BINARY_VERSION 1.0.0)

set(ANJAY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ANJAY_BUILD_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/lib")

set(CMAKE_USE_RELATIVE_PATHS TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/install_utils.cmake)

include(CheckFunctionExists)

# On Linux, one needs to link libdl to use dlsym(). On BSD, it is not necessary,
# and even harmful, since libdl does not exist.
set(CMAKE_REQUIRED_INCLUDES "dlfcn.h")
foreach(lib "" dl)
    message(STATUS "Looking for dlsym() in library: ${lib}")
    set(CMAKE_REQUIRED_LIBRARIES ${lib})

    # check_function_exists caches its result; make sure the check is
    # actually repeated for each lib
    unset(HAVE_DLSYM CACHE)
    check_function_exists(dlsym HAVE_DLSYM)
    set(CMAKE_REQUIRED_LIBRARIES)

    if(HAVE_DLSYM)
        set(DETECTED_DLSYM_LIBRARY "${lib}" CACHE STRING "" FORCE)
        break()
    endif()
endforeach()
set(CMAKE_REQUIRED_INCLUDES)
set(DLSYM_LIBRARY "${DETECTED_DLSYM_LIBRARY}" CACHE STRING "Name of the library containing dlsym() symbol")

################# FEATURES #####################################################

include(CMakeDependentOption)

# compilation flags
if(NOT CMAKE_VERSION VERSION_LESS 3.1)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_EXTENSIONS OFF)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
endif()

option(WITH_EXTRA_WARNINGS "Enable extra compilation warnings" OFF)
if(WITH_EXTRA_WARNINGS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Winit-self -Wmissing-declarations -Wc++-compat -Wsign-conversion -Wconversion -Wcast-qual -Wvla -Wno-variadic-macros -Wno-long-long")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wjump-misses-init")
    endif()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/PosixFeatures.cmake)

option(WITH_LIBRARY_SHARED "Compile Anjay as shared library" "${UNIX}")

option(WITH_ACCESS_CONTROL "Enable core support for Access Control mechanism" ON)
option(WITH_ATTR_STORAGE "Enable Attribute storage module" ON)
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
option(WITH_JSON "Enable support for JSON content format (output only)" ON)
option(WITH_AVS_PERSISTENCE "Enable support for persisting objects data" ON)


cmake_dependent_option(WITH_BLOCK_DOWNLOAD "Enable support for CoAP(S) downloads" ON "WITH_DOWNLOADER" OFF)
cmake_dependent_option(WITH_HTTP_DOWNLOAD "Enable support for HTTP(S) downloads" OFF "WITH_DOWNLOADER" OFF)

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

set(DTLS_SESSION_BUFFER_SIZE 1024 CACHE STRING
    "Size of the buffer that caches DTLS session information for resumption support.")

################# CONVENIENCE SUPPORT ##########################################

macro(make_absolute_sources ABSVAR)
    set(${ABSVAR})
    foreach(SRC ${ARGN})
        set(${ABSVAR} ${${ABSVAR}} ${CMAKE_CURRENT_SOURCE_DIR}/${SRC})
    endforeach()
endmacro()

################# CODE #########################################################

set(CORE_SOURCES
    src/access_utils.c
    src/anjay_core.c
    src/coap/id_source/auto.c
    src/coap/id_source/static.c
    src/coap/stream/client_internal.c
    src/coap/stream/common.c
    src/coap/stream/in.c
    src/coap/stream/out.c
    src/coap/stream/server_internal.c
    src/coap/stream/stream_internal.c
    src/dm_core.c
    src/dm/dm_attributes.c
    src/dm/dm_execute.c
    src/dm/dm_handlers.c
    src/dm/modules.c
    src/dm/query.c
    src/interface/register.c
    src/io/base64_out.c
    src/io_core.c
    src/io/dynamic.c
    src/io/opaque.c
    src/io/output_buf.c
    src/io/text.c
    src/io/tlv_in.c
    src/io/tlv_out.c
    src/io_utils.c
    src/notify.c
    src/raw_buffer.c
    src/sched.c
    src/servers/activate.c
    src/servers/connections.c
    src/servers/connection_udp.c
    src/servers/offline.c
    src/servers/register_internal.c
    src/servers/reload.c
    src/servers/server_connections.c
    src/servers/servers_internal.c
    src/servers_utils.c
    src/utils_core.c)

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
        src/coap/block/request.c
        src/coap/block/response.c
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
        src/observe/observe_core.c
        src/observe/observe_io.c)
endif()
if(WITH_JSON OR WITH_SENML_JSON)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/io/json_out.c)
endif()
set(CORE_PRIVATE_HEADERS
    src/access_utils.h
    src/anjay_core.h
    src/coap/block/request.h
    src/coap/block/response.h
    src/coap/block/transfer.h
    src/coap/block/transfer_impl.h
    src/coap/coap_log.h
    src/coap/coap_stream.h
    src/coap/content_format.h
    src/coap/id_source/auto.h
    src/coap/id_source/id_source.h
    src/coap/id_source/static.h
    src/coap/stream/client_internal.h
    src/coap/stream/common.h
    src/coap/stream/in.h
    src/coap/stream/out.h
    src/coap/stream/server_internal.h
    src/coap/stream/stream_internal.h
    src/dm/discover.h
    src/dm/dm_attributes.h
    src/dm/dm_execute.h
    src/dm/query.h
    src/dm_core.h
    src/downloader.h
    src/downloader/private.h
    src/interface/bootstrap_core.h
    src/interface/register.h
    src/io/base64_out.h
    src/io/tlv.h
    src/io/vtable.h
    src/io_core.h
    src/observe/observe_core.h
    src/observe/observe_internal.h
    src/sched_internal.h
    src/servers.h
    src/servers/activate.h
    src/servers/connections.h
    src/servers/connections_internal.h
    src/servers/register_internal.h
    src/servers/reload.h
    src/servers/server_connections.h
    src/servers/servers_internal.h
    src/servers_utils.h
    src/utils_core.h)
set(CORE_MODULES_HEADERS
    include_modules/anjay_modules/dm/attributes.h
    include_modules/anjay_modules/dm/execute.h
    include_modules/anjay_modules/dm/modules.h
    include_modules/anjay_modules/dm_utils.h
    include_modules/anjay_modules/downloader.h
    include_modules/anjay_modules/interface/bootstrap.h
    include_modules/anjay_modules/io_utils.h
    include_modules/anjay_modules/notify.h
    include_modules/anjay_modules/observe.h
    include_modules/anjay_modules/raw_buffer.h
    include_modules/anjay_modules/sched.h
    include_modules/anjay_modules/servers.h
    include_modules/anjay_modules/time_defs.h
    include_modules/anjay_modules/utils_core.h)
set(CORE_PUBLIC_HEADERS
    include_public/anjay/anjay.h
    include_public/anjay/core.h
    include_public/anjay/dm.h
    include_public/anjay/download.h
    include_public/anjay/io.h
    include_public/anjay/persistence.h
    include_public/anjay/stats.h)


set(ALL_SOURCES
    ${CORE_SOURCES}
    ${CORE_PRIVATE_HEADERS}
    ${CORE_MODULES_HEADERS}
    ${CORE_PUBLIC_HEADERS})
make_absolute_sources(ABSOLUTE_SOURCES ${ALL_SOURCES})

set(TEST_SOURCES
    ${ALL_SOURCES}
    src/coap/test/block_response.c
    src/coap/test/servers.c
    src/coap/test/servers.h
    src/coap/test/stream.c
    src/coap/test/utils.h
    src/downloader/test/downloader_mock.h
    src/interface/test/bootstrap_mock.h
    src/io/test/bigdata.h
    src/observe/test/observe_mock.h
    test/include/anjay_test/coap/socket.h
    test/include/anjay_test/coap/stream.h
    test/include/anjay_test/dm.h
    test/include/anjay_test/mock_clock.h
    test/include/anjay_test/mock_dm.h
    test/include/anjay_test/utils.h
    test/src/coap/socket.c
    test/src/coap/stream.c
    test/src/dm.c
    test/src/mock_clock.c
    test/src/mock_dm.c)
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
        set(PUBLIC_INCLUDE_DIRS ${PUBLIC_INCLUDE_DIRS}
                                ${CMAKE_CURRENT_SOURCE_DIR}/modules/${MOD_NAME}/include_public)
    endif()
endmacro()

if(WITH_ATTR_STORAGE)
    if(WITH_AVS_PERSISTENCE)
        DEFINE_MODULE(attr_storage ON "Automatic attribute storage module")
    else()
        message(FATAL_ERROR "Attribute storage module cannot be used if persistence is turned off")
    endif()
endif()
if(WITH_ACCESS_CONTROL)
    DEFINE_MODULE(access_control ON "Access control object implementation module")
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
set(AVS_COMMONS_COMPONENTS algorithm coap list vector rbtree buffer net stream stream_net utils compat_threading)
if(WITH_HTTP_DOWNLOAD)
    list(APPEND AVS_COMMONS_COMPONENTS http)
endif()
if(WITH_AVS_PERSISTENCE)
    list(APPEND AVS_COMMONS_COMPONENTS persistence)
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
    # WITH_TEST is also defined in Commons
    # it defaults to ON there, but if we have external avs_commons, let's better not depend on avs_unit by default
    cmake_dependent_option(WITH_TEST "Enable Anjay tests" OFF WITH_AVS_UNIT OFF)
    if(WITH_TEST)
        set(AVS_COMMONS_COMPONENTS ${AVS_COMMONS_COMPONENTS} unit)

        find_program(VALGRIND_EXECUTABLE valgrind)
        cmake_dependent_option(WITH_VALGRIND "Enable usage of valgrind during unit tests" OFF "VALGRIND_EXECUTABLE" OFF)
    endif()
    find_package(avs_commons COMPONENTS ${AVS_COMMONS_COMPONENTS} REQUIRED)
else()
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

if(WITH_AVS_LOG)
    set(DEPS_LIBRARIES_WEAK ${DEPS_LIBRARIES_WEAK} avs_log)
endif()

################# PUBLIC/MODULE INCLUDE DIRS ###################################

set(PUBLIC_INCLUDE_DIRS ${PUBLIC_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/include_public")
set(MODULES_INCLUDE_DIRS ${MODULES_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/include_modules")

include_directories(${PUBLIC_INCLUDE_DIRS}
                    ${MODULES_INCLUDE_DIRS})

################# FEATURE LIST LOG #############################################

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/config/anjay_config.h.in" CONFIG_FILE_CONTENTS)
set(CONFIG_FILE_VARIABLES)
foreach(CONFIG_FILE_LINE IN LISTS CONFIG_FILE_CONTENTS)
    if(CONFIG_FILE_LINE MATCHES "\#[ \t]*define[ \t]+([A-Za-z_0-9]+)[ \t]+\@([A-Za-z_0-9]+)\@")
        set(CONFIG_FILE_VARIABLE "${CMAKE_MATCH_1}")
        set(CONFIG_FILE_VARIABLE_TYPE "VALUE")
    elseif(CONFIG_FILE_LINE MATCHES "#[ \t]*cmakedefine[ \t]+([A-Za-z_0-9]+)")
        set(CONFIG_FILE_VARIABLE "${CMAKE_MATCH_1}")
        set(CONFIG_FILE_VARIABLE_TYPE "FLAG")
    elseif(CONFIG_FILE_LINE MATCHES "#[ \t]*cmakedefine" OR CONFIG_FILE_LINE MATCHES "\@[A-Za-z_0-9]+\@")
        message(SEND_ERROR "Found unloggable line in anjay_config.h.in: ${CONFIG_FILE_LINE}")
    else()
        set(CONFIG_FILE_VARIABLE "")
    endif()
    if(NOT CONFIG_FILE_VARIABLE MATCHES "^(ANJAY_VERSION|ANJAY_LOG_FEATURE_LIST|)$")
        list(APPEND CONFIG_FILE_VARIABLES "${CONFIG_FILE_VARIABLE} ${CONFIG_FILE_VARIABLE_TYPE}")
    endif()
endforeach()
list(SORT CONFIG_FILE_VARIABLES)

set(ANJAY_LOG_FEATURE_LIST "")
foreach(CONFIG_FILE_VARIABLE_ENTRY IN LISTS CONFIG_FILE_VARIABLES)
    string(REPLACE " " ";" CONFIG_FILE_VARIABLE_ENTRY "${CONFIG_FILE_VARIABLE_ENTRY}")
    list(GET CONFIG_FILE_VARIABLE_ENTRY 0 CONFIG_FILE_VARIABLE)
    list(GET CONFIG_FILE_VARIABLE_ENTRY 1 CONFIG_FILE_VARIABLE_TYPE)

    if(CONFIG_FILE_VARIABLE_TYPE MATCHES "^VALUE$")
        set(ANJAY_LOG_FEATURE_LIST "${ANJAY_LOG_FEATURE_LIST}\n    _anjay_log(anjay, TRACE, \"${CONFIG_FILE_VARIABLE} = \" AVS_QUOTE_MACRO(${CONFIG_FILE_VARIABLE}));")
    else()
        set(ANJAY_LOG_FEATURE_LIST "${ANJAY_LOG_FEATURE_LIST}\n#ifdef ${CONFIG_FILE_VARIABLE}")
        set(ANJAY_LOG_FEATURE_LIST "${ANJAY_LOG_FEATURE_LIST}\n    _anjay_log(anjay, TRACE, \"${CONFIG_FILE_VARIABLE} = ON\");")
        set(ANJAY_LOG_FEATURE_LIST "${ANJAY_LOG_FEATURE_LIST}\n#else // ${CONFIG_FILE_VARIABLE}")
        set(ANJAY_LOG_FEATURE_LIST "${ANJAY_LOG_FEATURE_LIST}\n    _anjay_log(anjay, TRACE, \"${CONFIG_FILE_VARIABLE} = OFF\");")
        set(ANJAY_LOG_FEATURE_LIST "${ANJAY_LOG_FEATURE_LIST}\n#endif // ${CONFIG_FILE_VARIABLE}")
    endif()
endforeach()

################# LINK #########################################################

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config/anjay_config.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/config/anjay_config.h")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config/anjay_config_log.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/config/anjay_config_log.h")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/config")

if(WITH_LIBRARY_SHARED)
    add_library(${PROJECT_NAME} SHARED ${ABSOLUTE_SOURCES})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${DEPS_LIBRARIES})
    target_link_libraries(${PROJECT_NAME} PUBLIC ${DEPS_LIBRARIES_WEAK})
    set_target_properties(${PROJECT_NAME} PROPERTIES
                          VERSION ${ANJAY_BINARY_VERSION})
    if(HAVE_VISIBILITY)
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

set(DEMO_DEPENDENCIES
    WITH_ACCESS_CONTROL
    WITH_AVS_LOG
    WITH_AVS_PERSISTENCE
    WITH_MODULE_access_control
    WITH_MODULE_attr_storage
    WITH_MODULE_fw_update
    WITH_MODULE_security
    WITH_MODULE_server)

cmake_dependent_option(WITH_DEMO "Compile DEMO applications" ON "${DEMO_DEPENDENCIES}" OFF)

if(WITH_DEMO)
    find_program(OPENSSL_EXECUTABLE openssl)
    if(OPENSSL_EXECUTABLE)
        if(NOT EXISTS "${ANJAY_BUILD_OUTPUT_DIR}/certs/client.crt.der")
            execute_process(COMMAND
                            env bash
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

cmake_dependent_option(WITH_INTEGRATION_TESTS "Enable integration tests" OFF "WITH_TEST;WITH_DEMO" OFF)

if(WITH_TEST)
    macro(add_anjay_test NAME)
        add_executable(${NAME}_test EXCLUDE_FROM_ALL ${ARGN})
        target_link_libraries(${NAME}_test avs_unit)

        if(NOT HAVE_DLSYM AND NOT DLSYM_LIBRARY)
            message(FATAL_ERROR "dlsym() is required for tests, but its definition "
                    "could not be found; either use -DDLSYM_LIBRARY to select the "
                    "library or disable tests")
        elseif(DLSYM_LIBRARY)
            target_link_libraries(${NAME}_test ${DLSYM_LIBRARY})
        endif()

        set_property(TARGET ${NAME}_test APPEND PROPERTY COMPILE_DEFINITIONS
                     ANJAY_TEST
                     "ANJAY_BIN_DIR=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\"")

        set(TEST_COMPILE_FLAGS " -Wno-overlength-strings -Wno-vla -Wno-c++-compat")
        if(CMAKE_C_COMPILER_ID MATCHES "GNU")
            string(APPEND TEST_COMPILE_FLAGS " -Wno-jump-misses-init")
            if(CMAKE_C_COMPILER_VERSION VERSION_LESS 4.8)
                string(APPEND TEST_COMPILE_FLAGS " -Wno-missing-field-initializers -Wno-uninitialized -Wno-unused-value")
            else()
                # before GCC 4.8 there was no -Wpedantic, only -pedantic; and no way to disable it
                string(APPEND TEST_COMPILE_FLAGS " -Wno-pedantic")
            endif()
        endif()
        set_property(TARGET ${NAME}_test APPEND PROPERTY COMPILE_FLAGS
                     "${TEST_COMPILE_FLAGS}")

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

    if(TARGET avs_commons_check)
        add_dependencies(check avs_commons_check)
    endif()

    # Source validation
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

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        option(WITH_LICENSE_TEST "Enable checking if all files have the Apache License boilerplate" OFF)
        if(WITH_LICENSE_TEST)
            add_custom_target(anjay_license_check
                              COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-license-headers.py"
                              WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_dependencies(anjay_unit_check anjay_license_check)
        endif()
    endif()

    # Symbol validation
    add_custom_target(symbols_check COMMAND ${CMAKE_CTEST_COMMAND} -R "'^test_.*_symbols$$'" --output-on-failure)

    if(WITH_LIBRARY_SHARED)
        add_test(NAME test_so_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_symbols.sh $<TARGET_FILE:${PROJECT_NAME}> anjay_ ANJAY_)
        add_dependencies(symbols_check ${PROJECT_NAME})
    endif()

    add_test(NAME test_a_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_symbols.sh $<TARGET_FILE:${PROJECT_NAME}_static> anjay_ ANJAY_ _anjay_ _ANJAY_)
    add_dependencies(symbols_check ${PROJECT_NAME}_static)

    add_dependencies(anjay_unit_check symbols_check)

    # Unit tests
    include(ProcessorCount)
    ProcessorCount(ANJAY_DEFAULT_NPROC)

    set(NPROC "${ANJAY_DEFAULT_NPROC}" CACHE STRING "Number of threads for multi-threaded build/test operations")

    if(WITH_VALGRIND)
        set(VALGRIND ${VALGRIND_EXECUTABLE} --leak-check=full --track-origins=yes -q --error-exitcode=63 --suppressions=${CMAKE_CURRENT_SOURCE_DIR}/valgrind_test.supp)
    endif()

    include_directories(test/include)
    add_anjay_test(${PROJECT_NAME} ${ABSOLUTE_TEST_SOURCES})
    target_link_libraries(${PROJECT_NAME}_test ${DEPS_LIBRARIES} ${DEPS_LIBRARIES_WEAK})

    add_subdirectory(test/codegen)

    if(WITH_INTEGRATION_TESTS)
        add_subdirectory(test/integration)
    endif()
else(WITH_TEST)
    macro(add_anjay_test NAME)
    endmacro()
endif(WITH_TEST)

################# FUZZ TESTING #################################################

add_subdirectory(test/fuzz)
add_subdirectory(doc)

################# STATIC ANALYSIS ##############################################

cmake_dependent_option(WITH_STATIC_ANALYSIS "Perform static analysis of the codebase on `make check`" OFF WITH_TEST OFF)
if(WITH_STATIC_ANALYSIS)
    find_program(SCAN_BUILD_BINARY scan-build)
    if(NOT SCAN_BUILD_BINARY)
        # some systems only have scan-build-x.y, where x.y is the version of LLVM
        # let's try that
        find_program(CLANG_BINARY clang)
        if(CLANG_BINARY)
            anjay_temp_name(_fname)
            file(WRITE ${_fname} "__clang_major__ __clang_minor__")
            execute_process(COMMAND "${CLANG_BINARY}" -E -P -x c ${_fname}
                            OUTPUT_VARIABLE CLANG_VERSION_OUTPUT
                            OUTPUT_STRIP_TRAILING_WHITESPACE)
            file(REMOVE ${_fname})
            string(REPLACE " " "." CLANG_VERSION_OUTPUT "${CLANG_VERSION_OUTPUT}")
            find_program(SCAN_BUILD_BINARY "scan-build-${CLANG_VERSION_OUTPUT}")
        endif()
    endif()
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

add_subdirectory(examples)

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
