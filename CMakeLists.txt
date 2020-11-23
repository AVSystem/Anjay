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
set(ANJAY_VERSION "2.8.0" CACHE STRING "Anjay library version")
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

################# TUNABLES #####################################################

set(MAX_PK_OR_IDENTITY_SIZE 2048 CACHE STRING
    "Maximum supported size (in bytes) of 'PK or Identity' Resource in Security object.")
set(MAX_SECRET_KEY_SIZE 256 CACHE STRING
    "Maximum supported size (in bytes) of 'Secret Key' Resource in Security object.")

# Following options refer to the payload of plaintext-encoded CoAP packets.
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

option(WITH_BOOTSTRAP "Enable LwM2M Bootstrap Interface support" ON)
option(WITH_DOWNLOADER "Enable support for downloader API" ON)
cmake_dependent_option(WITH_HTTP_DOWNLOAD "Enable support for HTTP(S) downloads" OFF "WITH_DOWNLOADER" OFF)

################# LIBRARIES ####################################################

option(WITH_AVS_LOG "Enable logging support" ON)
option(WITH_AVS_PERSISTENCE "Enable support for persisting objects data" ON)

# avs_commons required components.
set(AVS_COMMONS_COMPONENTS algorithm crypto list rbtree buffer net sched stream stream_net url utils compat_threading)
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

    function(read_avs_commons_compile_time_option OPTION_NAME)
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/avs_commons_check.c
             "#include <avsystem/commons/avs_commons_config.h>\nint main() {\n#ifndef ${OPTION_NAME}\nint error[-1];\n#endif\nreturn 0; }\n")
        get_target_property(INCLUDE_DIRS avs_utils INTERFACE_INCLUDE_DIRECTORIES)
        try_compile(OPTION_VALUE
                    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp
                    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/avs_commons_check.c
                    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${INCLUDE_DIRS}")
        set(${OPTION_NAME} ${OPTION_VALUE} CACHE INTERNAL "")
    endfunction()

    read_avs_coap_compile_time_option(AVS_COMMONS_HAVE_VISIBILITY)
    read_avs_coap_compile_time_option(AVS_COMMONS_STREAM_WITH_FILE)
    set(WITH_AVS_STREAM_FILE "${AVS_COMMONS_STREAM_WITH_FILE}")
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

    set(WITH_SCHEDULER_THREAD_SAFE OFF CACHE INTERNAL "")

    add_subdirectory(deps/avs_commons)
endif()

option(WITH_LOCAL_AVS_COAP "Use locally installed avs_coap library" OFF)
if(WITH_LOCAL_AVS_COAP)
    find_package(avs_coap REQUIRED)

    function(read_avs_coap_compile_time_option OPTION_NAME)
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/avs_coap_check.c
             "#include <avsystem/coap/avs_coap_config.h>\nint main() {\n#ifndef ${OPTION_NAME}\nint error[-1];\n#endif\nreturn 0; }\n")
        get_target_property(INCLUDE_DIRS avs_coap INTERFACE_INCLUDE_DIRECTORIES)
        try_compile(OPTION_VALUE
                    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp
                    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/avs_coap_check.c
                    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${INCLUDE_DIRS}")
        set(${OPTION_NAME} ${OPTION_VALUE} CACHE INTERNAL "")
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
option(WITH_DISCOVER "Enable support for LwM2M Discover operation" ON)
cmake_dependent_option(WITH_OBSERVE "Enable support for Information Reporting interface (Observe)" ON "WITH_AVS_COAP_OBSERVE" OFF)
cmake_dependent_option(WITH_CON_ATTR "Enable support for a custom attribute that controls Confirmable notifications" OFF WITH_OBSERVE OFF)
option(WITH_LEGACY_CONTENT_FORMAT_SUPPORT
       "Enable support for pre-LwM2M 1.0 CoAP Content-Format values (1541-1543)" OFF)
option(WITH_LWM2M_JSON "Enable support for LwM2M 1.0 JSON (output only)" ON)
option(WITHOUT_TLV "Disable support for TLV content format" OFF)
option(WITHOUT_PLAINTEXT "Disable support for Plain Text content format" OFF)
option(WITHOUT_DEREGISTER "Disable use of the Deregister message" OFF)

cmake_dependent_option(WITH_OBSERVATION_STATUS "Enable support for anjay_resource_observation_status() API" ON "WITH_OBSERVE" OFF)
cmake_dependent_option(WITH_COAP_DOWNLOAD "Enable support for CoAP(S) downloads" ON WITH_DOWNLOADER OFF)

cmake_dependent_option(WITH_ANJAY_LOGS "Enable logging support" ON WITH_AVS_LOG OFF)
cmake_dependent_option(WITH_ANJAY_TRACE_LOGS "Enable logging support" ON WITH_ANJAY_LOGS OFF)

cmake_dependent_option(AVS_LOG_WITH_TRACE "Enable TRACE level logging" OFF WITH_AVS_LOG OFF)
cmake_dependent_option(WITH_INTERNAL_LOGS "Enable logging from inside AVSystem Commons libraries" ON WITH_AVS_LOG OFF)
cmake_dependent_option(WITH_INTERNAL_TRACE "Enable TRACE-level logs inside AVSystem Commons libraries" ON AVS_LOG_WITH_TRACE OFF)

option(WITH_NET_STATS "Enable measuring amount of LwM2M traffic" ON)

################# MODULES ######################################################


cmake_dependent_option(WITH_MODULE_attr_storage "Automatic attribute storage module" ON WITH_AVS_PERSISTENCE OFF)
cmake_dependent_option(WITH_MODULE_access_control "Access control object implementation module" ON WITH_ACCESS_CONTROL OFF)
option(WITH_MODULE_security "Security object module" ON)
option(WITH_MODULE_server "Server object module" ON)
cmake_dependent_option(WITH_MODULE_fw_update "Firmware Update object module" ON WITH_DOWNLOADER OFF)

################# CODE #########################################################

add_library(anjay
            src/core/anjay_access_utils.c
            src/core/anjay_bootstrap_core.c
            src/core/anjay_core.c
            src/core/anjay_dm_core.c
            src/core/anjay_io_core.c
            src/core/anjay_io_utils.c
            src/core/anjay_notify.c
            src/core/anjay_raw_buffer.c
            src/core/anjay_servers_utils.c
            src/core/anjay_stats.c
            src/core/anjay_utils_core.c
            src/core/dm/anjay_discover.c
            src/core/dm/anjay_dm_attributes.c
            src/core/dm/anjay_dm_create.c
            src/core/dm/anjay_dm_execute.c
            src/core/dm/anjay_dm_handlers.c
            src/core/dm/anjay_dm_read.c
            src/core/dm/anjay_dm_write_attrs.c
            src/core/dm/anjay_dm_write.c
            src/core/dm/anjay_modules.c
            src/core/dm/anjay_query.c
            src/core/downloader/anjay_coap.c
            src/core/downloader/anjay_downloader.c
            src/core/downloader/anjay_http.c
            src/core/io/anjay_base64_out.c
            src/core/io/anjay_batch_builder.c
            src/core/io/anjay_common.c
            src/core/io/anjay_dynamic.c
            src/core/io/anjay_json_encoder.c
            src/core/io/anjay_opaque.c
            src/core/io/anjay_output_buf.c
            src/core/io/anjay_senml_like_encoder.c
            src/core/io/anjay_senml_like_out.c
            src/core/io/anjay_text.c
            src/core/io/anjay_tlv_in.c
            src/core/io/anjay_tlv_out.c
            src/core/observe/anjay_observe_core.c
            src/core/servers/anjay_activate.c
            src/core/servers/anjay_connection_ip.c
            src/core/servers/anjay_connections.c
            src/core/servers/anjay_register.c
            src/core/servers/anjay_reload.c
            src/core/servers/anjay_security_generic.c
            src/core/servers/anjay_server_connections.c
            src/core/servers/anjay_servers_internal.c
            src/modules/access_control/anjay_access_control_handlers.c
            src/modules/access_control/anjay_access_control_persistence.c
            src/modules/access_control/anjay_mod_access_control.c
            src/modules/attr_storage/anjay_attr_storage_persistence.c
            src/modules/attr_storage/anjay_mod_attr_storage.c
            src/modules/fw_update/anjay_fw_update.c
            src/modules/security/anjay_mod_security.c
            src/modules/security/anjay_security_persistence.c
            src/modules/security/anjay_security_transaction.c
            src/modules/security/anjay_security_utils.c
            src/modules/server/anjay_mod_server.c
            src/modules/server/anjay_server_persistence.c
            src/modules/server/anjay_server_transaction.c
            src/modules/server/anjay_server_utils.c
            src/anjay_config_log.h
            src/anjay_init.h
            src/core/anjay_access_utils_private.h
            src/core/anjay_bootstrap_core.h
            src/core/anjay_core.h
            src/core/anjay_dm_core.h
            src/core/anjay_downloader.h
            src/core/anjay_io_core.h
            src/core/anjay_servers_inactive.h
            src/core/anjay_servers_private.h
            src/core/anjay_servers_utils.h
            src/core/anjay_stats.h
            src/core/anjay_utils_private.h
            src/core/coap/anjay_content_format.h
            src/core/coap/anjay_msg_details.h
            src/core/dm/anjay_discover.h
            src/core/dm/anjay_dm_attributes.h
            src/core/dm/anjay_dm_create.h
            src/core/dm/anjay_dm_execute.h
            src/core/dm/anjay_dm_read.h
            src/core/dm/anjay_dm_write.h
            src/core/dm/anjay_dm_write_attrs.h
            src/core/dm/anjay_query.h
            src/core/downloader/anjay_private.h
            src/core/io/anjay_base64_out.h
            src/core/io/anjay_batch_builder.h
            src/core/io/anjay_common.h
            src/core/io/anjay_senml_like_encoder.h
            src/core/io/anjay_senml_like_encoder_vtable.h
            src/core/io/anjay_tlv.h
            src/core/io/anjay_vtable.h
            src/core/observe/anjay_observe_core.h
            src/core/observe/anjay_observe_internal.h
            src/core/servers/anjay_activate.h
            src/core/servers/anjay_connections.h
            src/core/servers/anjay_connections_internal.h
            src/core/servers/anjay_register.h
            src/core/servers/anjay_reload.h
            src/core/servers/anjay_security.h
            src/core/servers/anjay_server_connections.h
            src/core/servers/anjay_servers_internal.h
            src/modules/access_control/anjay_mod_access_control.h
            src/modules/attr_storage/anjay_mod_attr_storage.h
            src/modules/security/anjay_mod_security.h
            src/modules/security/anjay_security_transaction.h
            src/modules/security/anjay_security_utils.h
            src/modules/server/anjay_mod_server.h
            src/modules/server/anjay_server_transaction.h
            src/modules/server/anjay_server_utils.h
            src/anjay_modules/anjay_access_utils.h
            src/anjay_modules/anjay_bootstrap.h
            src/anjay_modules/anjay_dm_utils.h
            src/anjay_modules/anjay_io_utils.h
            src/anjay_modules/anjay_notify.h
            src/anjay_modules/anjay_raw_buffer.h
            src/anjay_modules/anjay_sched.h
            src/anjay_modules/anjay_servers.h
            src/anjay_modules/anjay_time_defs.h
            src/anjay_modules/anjay_utils_core.h
            src/anjay_modules/dm/anjay_attributes.h
            src/anjay_modules/dm/anjay_execute.h
            src/anjay_modules/dm/anjay_modules.h
            include_public/anjay/access_control.h
            include_public/anjay/anjay.h
            include_public/anjay/attr_storage.h
            include_public/anjay/core.h
            include_public/anjay/dm.h
            include_public/anjay/download.h
            include_public/anjay/fw_update.h
            include_public/anjay/io.h
            include_public/anjay/security.h
            include_public/anjay/server.h
            include_public/anjay/stats.h)

target_include_directories(anjay PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include_public>
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include_public>
                           $<INSTALL_INTERFACE:include>)
target_include_directories(anjay PRIVATE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_link_libraries(anjay PUBLIC avs_coap ${AVS_COMMONS_LIBRARIES})
set_property(TARGET anjay APPEND PROPERTY COMPILE_DEFINITIONS "ANJAY_VERSION=\"${ANJAY_VERSION}\"")

################# LINK #########################################################

set(ANJAY_WITH_ACCESS_CONTROL "${WITH_ACCESS_CONTROL}")
set(ANJAY_WITH_BOOTSTRAP "${WITH_BOOTSTRAP}")
set(ANJAY_WITH_COAP_DOWNLOAD "${WITH_COAP_DOWNLOAD}")
set(ANJAY_WITH_CON_ATTR "${WITH_CON_ATTR}")
set(ANJAY_WITH_DISCOVER "${WITH_DISCOVER}")
set(ANJAY_WITH_DOWNLOADER "${WITH_DOWNLOADER}")
set(ANJAY_WITH_HTTP_DOWNLOAD "${WITH_HTTP_DOWNLOAD}")
set(ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT "${WITH_LEGACY_CONTENT_FORMAT_SUPPORT}")
set(ANJAY_WITH_LOGS "${WITH_ANJAY_LOGS}")
set(ANJAY_WITH_LWM2M_JSON "${WITH_LWM2M_JSON}")
set(ANJAY_WITHOUT_TLV "${WITHOUT_TLV}")
set(ANJAY_WITHOUT_PLAINTEXT "${WITHOUT_PLAINTEXT}")
set(ANJAY_WITHOUT_DEREGISTER "${WITHOUT_DEREGISTER}")
set(ANJAY_WITH_MODULE_ACCESS_CONTROL "${WITH_MODULE_access_control}")
set(ANJAY_WITH_MODULE_ATTR_STORAGE "${WITH_MODULE_attr_storage}")
set(ANJAY_WITH_MODULE_FW_UPDATE "${WITH_MODULE_fw_update}")
set(ANJAY_WITH_MODULE_SECURITY "${WITH_MODULE_security}")
set(ANJAY_WITH_MODULE_SERVER "${WITH_MODULE_server}")
set(ANJAY_WITH_NET_STATS "${WITH_NET_STATS}")
set(ANJAY_WITH_OBSERVATION_STATUS "${WITH_OBSERVATION_STATUS}")
set(ANJAY_WITH_OBSERVE "${WITH_OBSERVE}")
set(ANJAY_WITH_TRACE_LOGS "${WITH_ANJAY_TRACE_LOGS}")


configure_file(include_public/anjay/anjay_config.h.in
               include_public/anjay/anjay_config.h)

if(BUILD_SHARED_LIBS)
    set_target_properties(anjay PROPERTIES
                          VERSION ${ANJAY_BINARY_VERSION})
    if(AVS_COMMONS_HAVE_VISIBILITY)
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
    WITH_AVS_STREAM_FILE
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
                   tests/core/coap/utils.c
                   tests/core/coap/utils.h
                   tests/core/downloader/downloader_mock.h
                   tests/core/bootstrap_mock.h
                   tests/core/io/bigdata.h
                   tests/core/observe/observe_mock.h
                   tests/utils/dm.c
                   tests/utils/dm.h
                   tests/utils/mock_clock.c
                   tests/utils/mock_clock.h
                   tests/utils/mock_dm.c
                   tests/utils/mock_dm.h
                   tests/utils/utils.h
                   tests/utils/coap/socket.h
                   tests/utils/coap/socket.c)
    target_include_directories(anjay_test PRIVATE
                               "${CMAKE_CURRENT_SOURCE_DIR}"
                               $<TARGET_PROPERTY:anjay,INCLUDE_DIRECTORIES>)
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
        add_test(NAME test_${F}_visibility COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_visibility.py ${F})
        add_test(NAME test_${F}_headers COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_headers.py ${F})
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
    add_test(NAME test_function_duplicates COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_duplicates.py ${ABSOLUTE_HEADERS})
    add_test(NAME test_markdown_toc COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/markdown-toc.py --check "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    add_test(NAME test_config_log COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/anjay_config_log_tool.py validate)

    add_custom_target(function_duplicates_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_function_duplicates$$'")

    add_custom_target(toc_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_markdown_toc$$'")

    add_custom_target(config_log_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_config_log$$'")

    add_dependencies(anjay_unit_check
                     function_duplicates_check
                     toc_check
                     config_log_check)

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
        add_test(NAME test_so_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_symbols.sh $<TARGET_FILE:anjay> anjay_ ANJAY_ "__odr_asan[.]")
    else()
        add_test(NAME test_a_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_symbols.sh $<TARGET_FILE:anjay> anjay_ ANJAY_ _anjay_ _ANJAY_ "__odr_asan[.]")
    endif()
    add_dependencies(symbols_check anjay)
    add_dependencies(anjay_unit_check symbols_check)

    add_custom_target(anjay_extern_c_check COMMAND
                      "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/tools/check-extern-c.py" --path "${CMAKE_CURRENT_SOURCE_DIR}")
    add_dependencies(anjay_unit_check anjay_extern_c_check)

    add_custom_target(anjay_filename_check
                      COMMAND ! find src -name "'*.[ch]'" | sed -e "'s|^.*/||'" | grep -v "'^anjay_'"
                      COMMAND ! find src -name "'*.[ch]'" | sed -e "'s|^.*/||'" | sort | uniq -c | grep -v "'^ *1 '"
                      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_dependencies(anjay_check anjay_filename_check)

    # Unit tests
    include(ProcessorCount)
    ProcessorCount(ANJAY_DEFAULT_NPROC)

    set(NPROC "${ANJAY_DEFAULT_NPROC}" CACHE STRING "Number of threads for multi-threaded build/test operations")

    add_subdirectory(tests/codegen)

    if(WITH_INTEGRATION_TESTS)
        add_subdirectory(tests/integration)
    endif()
else(WITH_TEST)
    macro(add_anjay_test NAME)
    endmacro()
endif(WITH_TEST)

################# FUZZ TESTING #################################################

add_subdirectory(tests/fuzz)
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
                              --ignore-symbol "^_avs_.*_cleanup_global.*_state$$"
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

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/include_public/anjay/anjay_config.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/anjay.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/core.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/dm.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/download.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/io.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/stats.h"
        DESTINATION include/anjay)
if(WITH_MODULE_access_control)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/access_control.h"
            DESTINATION include/anjay)
endif()
if(WITH_MODULE_attr_storage)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/attr_storage.h"
            DESTINATION include/anjay)
endif()
if(WITH_MODULE_fw_update)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/fw_update.h"
            DESTINATION include/anjay)
endif()
if(WITH_MODULE_security)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/security.h"
            DESTINATION include/anjay)
endif()
if(WITH_MODULE_server)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include_public/anjay/server.h"
            DESTINATION include/anjay)
endif()

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
