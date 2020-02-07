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

project(anjay C)
set(ANJAY_VERSION "2.2.5" CACHE STRING "Anjay library version")
set(ANJAY_BINARY_VERSION 1.0.0)

set(ANJAY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ANJAY_BUILD_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/lib")

set(CMAKE_USE_RELATIVE_PATHS TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Winit-self -Wmissing-declarations -Wc++-compat -Wsign-conversion -Wconversion -Wcast-qual -Wvla -Wno-variadic-macros -Wno-long-long -Wshadow")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wjump-misses-init")
    endif()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/deps/avs_commons/cmake/PosixFeatures.cmake)

option(WITH_LIBRARY_SHARED "Compile Anjay as shared library" "${UNIX}")
if(WITH_LIBRARY_SHARED)
    # It is not possible to create a shared library if -fPIC is not enabled for
    # either Anjay or any of its dependencies.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

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

################# FEATURES THAT REQUIRE LIBRARY CONFIGURATION ##################

option(WITH_DOWNLOADER "Enable support for downloader API" ON)
cmake_dependent_option(WITH_HTTP_DOWNLOAD "Enable support for HTTP(S) downloads" OFF "WITH_DOWNLOADER" OFF)

################# LIBRARIES ####################################################

option(WITH_AVS_LOG "Enable logging support" ON)
option(WITH_AVS_PERSISTENCE "Enable support for persisting objects data" ON)

# avs_commons required components.
set(AVS_COMMONS_COMPONENTS algorithm list vector rbtree buffer net sched stream stream_net utils compat_threading)
if(WITH_HTTP_DOWNLOAD)
    list(APPEND AVS_COMMONS_COMPONENTS http)
endif()
if(WITH_AVS_PERSISTENCE)
    list(APPEND AVS_COMMONS_COMPONENTS persistence)
endif()

set(AVS_COMMONS_LIBRARIES)
foreach(_component ${AVS_COMMONS_COMPONENTS})
    string(TOUPPER ${_component} _component_uppercase)

    # Enable compilation of each component.
    set(WITH_AVS_${_component_uppercase} ON CACHE INTERNAL "")

    # And mark it as a weak dependency.
    list(APPEND AVS_COMMONS_LIBRARIES avs_${_component})
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

    # Used in fw_update to determine supported download protocols
    set(WITH_SSL OFF CACHE INTERNAL "")
    if(WITH_MBEDTLS OR WITH_OPENSSL OR WITH_TINYDTLS)
        set(WITH_SSL ON CACHE INTERNAL "" FORCE)
    endif()

    message(STATUS "DTLS backend: ${_DTLS_BACKEND_LOWERCASE}")

    set(WITH_SCHEDULER_THREAD_SAFE OFF CACHE INTERNAL "")

    add_subdirectory(deps/avs_commons)
endif()

option(WITH_LOCAL_AVS_COAP "Use locally installed avs_coap library" OFF)
if(WITH_LOCAL_AVS_COAP)
    find_package(avs_coap REQUIRED)

    function(read_avs_coap_compile_time_option OPTION_NAME)
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/avs_coap_check.c
             "#include <avsystem/coap/config.h>\nint main() {\n#ifndef ${OPTION_NAME}\nint error[-1];\n#endif\nreturn 0; }\n")
        get_target_property(INCLUDE_DIRS avs_coap INTERFACE_INCLUDE_DIRECTORIES)
        try_compile(OPTION_VALUE
                    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp
                    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/avs_coap_check.c
                    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${INCLUDE_DIRS}")
        set(${OPTION_NAME} ${OPTION_VALUE} PARENT_SCOPE)
    endfunction()

    read_avs_coap_compile_time_option(WITH_AVS_COAP_UDP)
    read_avs_coap_compile_time_option(WITH_AVS_COAP_OBSERVE)
    read_avs_coap_compile_time_option(WITH_AVS_COAP_BLOCK)
    read_avs_coap_compile_time_option(WITH_AVS_COAP_STREAMING_API)
else()
    add_subdirectory(deps/avs_coap)
endif()

if(NOT WITH_AVS_COAP_STREAMING_API)
    message(FATAL_ERROR "avs_coap streaming API is required, but disabled")
endif()

if(WITH_AVS_LOG)
    list(APPEND AVS_COMMONS_LIBRARIES avs_log)
endif()

################# FEATURES #####################################################

option(WITH_ACCESS_CONTROL "Enable core support for Access Control mechanism" ON)
option(WITH_ATTR_STORAGE "Enable Attribute storage module" ON)
option(WITH_BOOTSTRAP "Enable LwM2M Bootstrap Interface support" ON)
option(WITH_DISCOVER "Enable support for LwM2M Discover operation" ON)
cmake_dependent_option(WITH_OBSERVE "Enable support for Information Reporting interface (Observe)" ON "WITH_AVS_COAP_OBSERVE" OFF)
if(WITH_OBSERVE)
    option(WITH_CON_ATTR
           "Enable support for a custom attribute that controls Confirmable notifications" OFF)
endif()
option(WITH_LEGACY_CONTENT_FORMAT_SUPPORT
       "Enable support for pre-LwM2M 1.0 CoAP Content-Format values (1541-1543)" OFF)
option(WITH_LWM2M_JSON "Enable support for LwM2M 1.0 JSON (output only)" ON)

cmake_dependent_option(WITH_OBSERVATION_STATUS "Enable support for anjay_resource_observation_status() API" ON "WITH_OBSERVE" OFF)
cmake_dependent_option(WITH_COAP_DOWNLOAD "Enable support for CoAP(S) downloads" ON WITH_DOWNLOADER OFF)

cmake_dependent_option(WITH_ANJAY_LOGS "Enable logging support" ON WITH_AVS_LOG OFF)
cmake_dependent_option(WITH_ANJAY_TRACE_LOGS "Enable logging support" ON WITH_ANJAY_LOGS OFF)

cmake_dependent_option(AVS_LOG_WITH_TRACE "Enable TRACE level logging" OFF WITH_AVS_LOG OFF)
cmake_dependent_option(WITH_INTERNAL_LOGS "Enable logging from inside AVSystem Commons libraries" ON WITH_AVS_LOG OFF)
cmake_dependent_option(WITH_INTERNAL_TRACE "Enable TRACE-level logs inside AVSystem Commons libraries" ON AVS_LOG_WITH_TRACE OFF)

option(WITH_NET_STATS "Enable measuring amount of LwM2M traffic" ON)

################# CODE #########################################################

add_library(anjay
            src/access_utils.c
            src/anjay_core.c
            src/dm_core.c
            src/dm/dm_attributes.c
            src/dm/dm_create.c
            src/dm/dm_execute.c
            src/dm/dm_handlers.c
            src/dm/dm_read.c
            src/dm/dm_write_attrs.c
            src/dm/dm_write.c
            src/dm/modules.c
            src/dm/query.c
            src/bootstrap_core.c
            src/io/base64_out.c
            src/io_core.c
            src/io/common.c
            src/io/dynamic.c
            src/io/opaque.c
            src/io/output_buf.c
            src/io/text.c
            src/io/tlv_in.c
            src/io/tlv_out.c
            src/io_utils.c
            src/notify.c
            src/raw_buffer.c
            src/servers/activate.c
            src/servers/connections.c
            src/servers/connection_ip.c
            src/servers/offline.c
            src/servers/register.c
            src/servers/reload.c
            src/servers/security_generic.c
            src/servers/server_connections.c
            src/servers/servers_internal.c
            src/servers_utils.c
            src/stats.c
            src/utils_core.c
            src/access_utils.h
            src/anjay_core.h
            src/coap/content_format.h
            src/coap/msg_details.h
            src/dm/discover.h
            src/dm/dm_attributes.h
            src/dm/dm_execute.h
            src/dm/query.h
            src/dm_core.h
            src/downloader.h
            src/downloader/private.h
            src/bootstrap_core.h
            src/io/base64_out.h
            src/io/tlv.h
            src/io/vtable.h
            src/io_core.h
            src/observe/observe_core.h
            src/observe/observe_internal.h
            src/servers.h
            src/servers/activate.h
            src/servers/connections.h
            src/servers/connections_internal.h
            src/servers/register.h
            src/servers/reload.h
            src/servers/security.h
            src/servers/server_connections.h
            src/servers/servers_internal.h
            src/servers_inactive.h
            src/servers_utils.h
            src/stats.h
            src/utils_core.h
            include_modules/anjay_modules/access_utils.h
            include_modules/anjay_modules/dm/attributes.h
            include_modules/anjay_modules/dm/execute.h
            include_modules/anjay_modules/dm/modules.h
            include_modules/anjay_modules/dm_utils.h
            include_modules/anjay_modules/io_utils.h
            include_modules/anjay_modules/notify.h
            include_modules/anjay_modules/raw_buffer.h
            include_modules/anjay_modules/sched.h
            include_modules/anjay_modules/servers.h
            include_modules/anjay_modules/time_defs.h
            include_modules/anjay_modules/utils_core.h
            include_public/anjay/anjay.h
            include_public/anjay/core.h
            include_public/anjay/dm.h
            include_public/anjay/download.h
            include_public/anjay/io.h
            include_public/anjay/stats.h)

if(WITH_DOWNLOADER)
    target_sources(anjay PRIVATE src/downloader/downloader.c)
endif()
if(WITH_COAP_DOWNLOAD)
    target_sources(anjay PRIVATE src/downloader/coap.c)
endif()
if(WITH_HTTP_DOWNLOAD)
    target_sources(anjay PRIVATE src/downloader/http.c)
endif()
if(WITH_DISCOVER)
    target_sources(anjay PRIVATE src/dm/discover.c)
endif()
if(WITH_OBSERVE)
    target_sources(anjay PRIVATE src/observe/observe_core.c)
endif()
if(WITH_LWM2M_JSON
   )
    target_sources(anjay PRIVATE
                   src/io/senml_like_encoder.c
                   src/io/senml_like_out.c)
endif()
if(WITH_LWM2M_JSON
   )
    target_sources(anjay PRIVATE src/io/json_encoder.c)
endif()
if(WITH_OBSERVE
   )
    target_sources(anjay PRIVATE src/io/batch_builder.c)
endif()

target_include_directories(anjay PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include_public>
                           $<INSTALL_INTERFACE:include>)
target_include_directories(anjay PRIVATE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include_modules>)
target_link_libraries(anjay PUBLIC avs_coap ${AVS_COMMONS_LIBRARIES})

################# MODULES ######################################################

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
if(WITH_DOWNLOADER)
    DEFINE_MODULE(fw_update ON "Firmware Update object module")
endif()


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

configure_file(config/anjay_config.h.in
               config/anjay_config.h)
configure_file(config/anjay_config_log.h.in
               config/anjay_config_log.h)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/config")

if(BUILD_SHARED_LIBS)
    set_target_properties(anjay PROPERTIES
                          VERSION ${ANJAY_BINARY_VERSION})
    if(HAVE_VISIBILITY)
        get_property(LINK_FLAGS TARGET anjay PROPERTY LINK_FLAGS)
        set_property(TARGET anjay PROPERTY LINK_FLAGS "${LINK_FLAGS} -Wl,--exclude-libs,ALL")
    endif()
    if(APPLE)
        get_property(LINK_FLAGS TARGET anjay PROPERTY LINK_FLAGS)
        set_property(TARGET anjay PROPERTY LINK_FLAGS "${LINK_FLAGS} -Wl,-undefined,dynamic_lookup")
    endif()
endif()

################# DEMO #########################################################

set(DEMO_DEPENDENCIES
    WITH_ACCESS_CONTROL
    WITH_AVS_LOG
    WITH_AVS_PERSISTENCE
    WITH_BOOTSTRAP
    WITH_OBSERVE
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

    add_subdirectory(demo)
endif()

################# TEST ########################################################

cmake_dependent_option(WITH_INTEGRATION_TESTS "Enable integration tests" OFF "WITH_TEST;WITH_DEMO" OFF)

if(WITH_TEST)
    enable_testing()

    add_custom_target(check)
    add_custom_target(anjay_unit_check)
    add_dependencies(check anjay_unit_check)

    # anjay_test
    add_executable(anjay_test EXCLUDE_FROM_ALL
                   $<TARGET_PROPERTY:anjay,SOURCES>
                   src/coap/test/utils.c
                   src/coap/test/utils.h
                   src/downloader/test/downloader_mock.h
                   src/test/bootstrap_mock.h
                   src/io/test/bigdata.h
                   src/observe/test/observe_mock.h
                   test/include/anjay_test/dm.h
                   test/include/anjay_test/mock_clock.h
                   test/include/anjay_test/mock_dm.h
                   test/include/anjay_test/utils.h
                   test/include/anjay_test/coap/socket.h
                   test/src/coap/socket.c
                   test/src/dm.c
                   test/src/mock_clock.c
                   test/src/mock_dm.c)
    target_include_directories(anjay_test PRIVATE test/include $<TARGET_PROPERTY:anjay,INCLUDE_DIRECTORIES>)
    target_link_libraries(anjay_test PRIVATE avs_unit avs_coap_for_tests ${AVS_COMMONS_LIBRARIES})

    if(NOT HAVE_DLSYM AND NOT DLSYM_LIBRARY)
        message(FATAL_ERROR "dlsym() is required for tests, but its definition "
                "could not be found; either use -DDLSYM_LIBRARY to select the "
                "library or disable tests")
    elseif(DLSYM_LIBRARY)
        target_link_libraries(anjay_test PRIVATE ${DLSYM_LIBRARY})
    endif()
    set_property(TARGET anjay_test APPEND PROPERTY COMPILE_DEFINITIONS
                 ANJAY_TEST
                 "ANJAY_BIN_DIR=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\"")

    target_compile_options(anjay_test PRIVATE -Wno-overlength-strings -Wno-vla -Wno-c++-compat)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        target_compile_options(anjay_test PRIVATE -Wno-jump-misses-init)
        if(CMAKE_C_COMPILER_VERSION VERSION_LESS 4.8)
            target_compile_options(anjay_test PRIVATE -Wno-missing-field-initializers -Wno-uninitialized -Wno-unused-value)
        else()
            # before GCC 4.8 there was no -Wpedantic, only -pedantic; and no way to disable it
            target_compile_options(anjay_test PRIVATE -Wno-pedantic)
        endif()
    endif()

    if(WITH_VALGRIND)
        set(VALGRIND ${VALGRIND_EXECUTABLE} --leak-check=full --track-origins=yes -q --error-exitcode=63 --suppressions=${CMAKE_CURRENT_SOURCE_DIR}/valgrind_test.supp)
    endif()

    if(VALGRIND)
        set(VALGRIND_LOG ${VALGRIND} --log-file=${ANJAY_BUILD_OUTPUT_DIR}/VALGRIND.anjay_test.log)
    else()
        set(VALGRIND_LOG)
    endif()

    add_test(NAME anjay_test
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
             COMMAND ${VALGRIND_LOG} $<TARGET_FILE:anjay_test>)

    add_custom_target(anjay_check COMMAND ${CMAKE_CTEST_COMMAND} -R "^anjay_test$$" -V DEPENDS anjay_test)
    add_dependencies(anjay_unit_check anjay_check)

    if(TARGET avs_commons_check)
        add_dependencies(check avs_commons_check)
    endif()
    # TODO T2192: rename to avs_coap after migration
    if(TARGET avs_coap_check)
        add_dependencies(check avs_coap_check)
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
        option(WITH_LICENSE_TEST "Enable checking if all files have the license boilerplate" OFF)
        if(WITH_LICENSE_TEST)
            add_custom_target(anjay_license_check
                              COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_license_headers.py"
                              WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_dependencies(anjay_unit_check anjay_license_check)
        endif()
    endif()

    # Symbol validation
    add_custom_target(symbols_check COMMAND ${CMAKE_CTEST_COMMAND} -R "'^test_.*_symbols$$'" --output-on-failure)

    if(BUILD_SHARED_LIBS)
        add_test(NAME test_so_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_symbols.sh $<TARGET_FILE:anjay> anjay_ ANJAY_)
    else()
        add_test(NAME test_a_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_symbols.sh $<TARGET_FILE:anjay> anjay_ ANJAY_ _anjay_ _ANJAY_)
    endif()
    add_dependencies(symbols_check anjay)
    add_dependencies(anjay_unit_check symbols_check)

    add_custom_target(anjay_extern_c_check COMMAND
                      "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/tools/check-extern-c.py" --path "${CMAKE_CURRENT_SOURCE_DIR}")
    add_dependencies(anjay_unit_check anjay_extern_c_check)

    # Unit tests
    include(ProcessorCount)
    ProcessorCount(ANJAY_DEFAULT_NPROC)

    set(NPROC "${ANJAY_DEFAULT_NPROC}" CACHE STRING "Number of threads for multi-threaded build/test operations")

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
            avs_temp_name(_fname)
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

cmake_dependent_option(WITH_FIND_UNUSED_CODE "Check for unused symbols on `make check`" ON "WITH_TEST;UNIX;NOT APPLE" OFF)
if(WITH_FIND_UNUSED_CODE)
    add_custom_target(find_unused_code
                      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/find_unused_code.py"
                              # avs_commons global cleanup functions
                              --ignore-symbol "^_avs_.*_cleanup_global_state$$"
                              # gcc internals
                              --ignore-object ".*/crtbegin.o"
                              # external libraries
                              --ignore-file "^/usr/lib/.*$$")
    add_dependencies(check find_unused_code)
endif()

add_subdirectory(examples)

################# INSTALL ######################################################

# libraries
install(TARGETS anjay EXPORT anjay-targets DESTINATION lib)

install(DIRECTORY include_public/anjay
        DESTINATION include
        FILES_MATCHING REGEX "[.]h$")

# install CMake package
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/anjay-config.cmake.in
               ${ANJAY_BUILD_OUTPUT_DIR}/cmake/anjay-config.cmake
               @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/anjay-version.cmake.in
               ${ANJAY_BUILD_OUTPUT_DIR}/cmake/anjay-version.cmake
               @ONLY)

install(EXPORT anjay-targets DESTINATION lib/anjay)
install(FILES
        ${ANJAY_BUILD_OUTPUT_DIR}/cmake/anjay-config.cmake
        ${ANJAY_BUILD_OUTPUT_DIR}/cmake/anjay-version.cmake
        DESTINATION lib/anjay)
