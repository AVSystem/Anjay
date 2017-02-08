cmake_minimum_required(VERSION 2.8.8)

option(WITH_FUZZ_TESTS "Compile fuzz tests using the AFL fuzzer" OFF)
if(WITH_FUZZ_TESTS)
    if(AFL_FUZZER_DIR)
        set(CMAKE_C_COMPILER "${AFL_FUZZER_DIR}/afl-gcc")
    else()
        set(CMAKE_C_COMPILER "afl-gcc")
    endif()
endif()

project(anjay C)
set(ANJAY_VERSION "1.0.0" CACHE STRING "Anjay library version")
set(ANJAY_BINARY_VERSION 1.0.0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")

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
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DEFAULT_SOURCE")

option(WITH_EXTRA_WARNINGS "Enable extra compilation warnings" OFF)
if(WITH_EXTRA_WARNINGS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Winit-self -Wmissing-declarations -Wc++-compat -Wsign-conversion -Wconversion -Wcast-qual -Wno-variadic-macros -Wno-long-long")
endif()

option(WITH_DEMO "Compile DEMO applications" ON)
option(WITH_LIBRARY_SHARED "Compile Anjay as shared library" ON)

option(WITH_ACCESS_CONTROL "Enable core support for Access Control mechanism" ON)
option(WITH_BLOCK_RECEIVE "Enable support for receiving CoAP BLOCK transfers" ON)
option(WITH_BLOCK_SEND "Enable support for sending data in CoAP BLOCK mode" ON)
option(WITH_BOOTSTRAP "Enable LWM2M Bootstrap Interface support" ON)
option(WITH_DISCOVER "Enable support for LWM2M Discover operation" ON)
option(WITH_OBSERVE "Enable support for Information Reporting interface (Observe)" ON)
option(WITH_LEGACY_CONTENT_FORMAT_SUPPORT
       "Enable support for pre-LwM2M 1.0 CoAP Content-Format values (1541-1543)" OFF)

option(WITH_AVS_LOG "Enable logging support" ON)

if(WITH_AVS_LOG)
    option(AVS_LOG_WITH_TRACE "Enable TRACE level logging" OFF)
endif()

# -fvisibility, #pragma GCC visibility
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/visibility.c
     "#pragma GCC visibility push(default)\nint f();\n#pragma GCC visibility push(hidden)\nint f() { return 0; }\n#pragma GCC visibility pop\nint main() { return f(); }\n\n")
try_compile(HAVE_VISIBILITY
            ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp
            ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/visibility.c
            COMPILE_DEFINITIONS -Wall -Wextra -Werror -fvisibility=default)

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
    src/coap/msg.c
    src/coap/msg_info.c
    src/coap/msg_builder.c
    src/coap/block_builder.c
    src/coap/opt.c
    src/coap/socket.c
    src/coap/id_source/auto.c
    src/coap/id_source/static.c
    src/coap/stream/client.c
    src/coap/stream/common.c
    src/coap/stream/in.c
    src/coap/stream/out.c
    src/coap/stream/server.c
    src/coap/stream/stream.c
    src/coap/utils.c
    src/interface/register.c
    src/io/dynamic.c
    src/io/opaque.c
    src/io/output_buf.c
    src/io/text.c
    src/io/tlv_in.c
    src/io/tlv_out.c
    src/dm.c
    src/dm/attributes.c
    src/dm/common.c
    src/dm/execute.c
    src/dm/handlers.c
    src/dm/query.c
    src/anjay.c
    src/io.c
    src/notify.c
    src/servers/activate.c
    src/servers/connection_info.c
    src/servers/offline.c
    src/servers/reload.c
    src/servers/register.c
    src/servers/servers.c
    src/raw_buffer.c
    src/sched.c
    src/utils.c)
if(WITH_ACCESS_CONTROL)
    set(CORE_SOURCES ${CORE_SOURCES} src/access_control.c)
endif()
if(WITH_BLOCK_SEND)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/coap/block/response.c
        src/coap/block/request.c
        src/coap/block/transfer.c)
endif()
if(WITH_BOOTSTRAP)
    set(CORE_SOURCES ${CORE_SOURCES} src/interface/bootstrap.c)
endif()
if(WITH_DISCOVER)
    set(CORE_SOURCES ${CORE_SOURCES} src/dm/discover.c)
endif()
if(WITH_OBSERVE)
    set(CORE_SOURCES ${CORE_SOURCES}
        src/observe.c
        src/observe_io.c)
endif()
set(CORE_PRIVATE_HEADERS
    src/access_control.h
    src/coap/block/request.h
    src/coap/block/response.h
    src/coap/block/transfer.h
    src/coap/block/transfer_impl.h
    src/coap/block_builder.h
    src/coap/id_source/id_source.h
    src/coap/id_source/auto.h
    src/coap/id_source/static.h
    src/coap/log.h
    src/coap/msg.h
    src/coap/msg_builder.h
    src/coap/msg_info.h
    src/coap/msg_internal.h
    src/coap/opt.h
    src/coap/socket.h
    src/coap/stream.h
    src/coap/stream/client.h
    src/coap/stream/common.h
    src/coap/stream/in.h
    src/coap/stream/out.h
    src/coap/stream/server.h
    src/coap/stream/stream.h
    src/coap/utils.h
    src/dm.h
    src/dm/attributes.h
    src/dm/discover.h
    src/dm/execute.h
    src/dm/query.h
    src/anjay.h
    src/interface/bootstrap.h
    src/interface/register.h
    src/io.h
    src/io/tlv.h
    src/io/vtable.h
    src/observe.h
    src/sched.h
    src/servers.h
    src/servers/activate.h
    src/servers/connection_info.h
    src/servers/register.h
    src/servers/servers.h
    src/utils.h)
set(CORE_MODULES_HEADERS
    include_modules/anjay_modules/dm.h
    include_modules/anjay_modules/dm/execute.h
    include_modules/anjay_modules/io.h
    include_modules/anjay_modules/notify.h
    include_modules/anjay_modules/observe.h
    include_modules/anjay_modules/time.h
    include_modules/anjay_modules/utils.h)
set(CORE_PUBLIC_HEADERS
    include_public/anjay/anjay.h)

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
    src/test/anjay.h
    src/test/mock_coap_stream.h
    src/test/mock_coap_stream_impl.h
    src/test/observe_mock.h
    test/src/coap/stream.c
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

################# LIBRARIES ####################################################

set(DEPS_LIBRARIES "")
set(DEPS_LIBRARIES_WEAK "")

# avs_commons required components.
set(AVS_COMMONS_COMPONENTS algorithm list vector rbtree buffer net stream)

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

    set(_DTLS_BACKENDS "mbedtls" "openssl")
    set(DTLS_BACKEND "mbedtls" CACHE STRING "DTLS backend to use; possible values: <empty> ${_DTLS_BACKENDS}")

    string(TOLOWER "${DTLS_BACKEND}" _DTLS_BACKEND_LOWERCASE)
    if(_DTLS_BACKEND_LOWERCASE STREQUAL "")
        set(WITH_OPENSSL OFF CACHE INTERNAL "")
        set(WITH_MBEDTLS OFF CACHE INTERNAL "")
    elseif(_DTLS_BACKEND_LOWERCASE STREQUAL "mbedtls")
        set(WITH_OPENSSL OFF CACHE INTERNAL "")
        set(WITH_MBEDTLS ON CACHE INTERNAL "")
    elseif(_DTLS_BACKEND_LOWERCASE STREQUAL "openssl")
        set(OPENSSL_CUSTOM_CIPHERS_ENABLED ON CACHE INTERNAL "")
        set(WITH_OPENSSL ON CACHE INTERNAL "")
        set(WITH_OPENSSL_CUSTOM_CIPHERS "ECDHE-ECDSA-AES128-CCM8:PSK-AES128-CCM8" CACHE INTERNAL "")
    else()
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
    target_link_libraries(${PROJECT_NAME} ${DEPS_LIBRARIES})
    target_link_libraries(${PROJECT_NAME} LINK_INTERFACE_LIBRARIES ${DEPS_LIBRARIES_WEAK})
    anjay_emit_deps(${PROJECT_NAME} ${DEPS_LIBRARIES_WEAK})
    set_target_properties(${PROJECT_NAME} PROPERTIES
                          VERSION ${ANJAY_BINARY_VERSION})
    if(HAVE_VISIBILITY)
        get_property(LINK_FLAGS TARGET ${PROJECT_NAME} PROPERTY LINK_FLAGS)
        set_property(TARGET ${PROJECT_NAME} PROPERTY LINK_FLAGS "${LINK_FLAGS} -Wl,--exclude-libs,ALL")
    endif()
endif()

add_library(${PROJECT_NAME}_static STATIC ${ABSOLUTE_SOURCES})
target_link_libraries(${PROJECT_NAME}_static ${DEPS_LIBRARIES} ${DEPS_LIBRARIES_WEAK})
anjay_emit_deps(${PROJECT_NAME}_static ${DEPS_LIBRARIES} ${DEPS_LIBRARIES_WEAK})

################# DEMO #########################################################

if(WITH_DEMO)
    find_program(OPENSSL_EXECUTABLE openssl)
    if(OPENSSL_EXECUTABLE)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo/ssl_key.h")
            execute_process(COMMAND
                            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate-certs.sh"
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

if(WITH_AVS_UNIT)
    add_subdirectory(tools)
endif()

macro(add_anjay_test NAME)
    add_executable(${NAME}_test EXCLUDE_FROM_ALL ${ARGN})
    target_link_libraries(${NAME}_test avs_unit dl)
    set_property(TARGET ${NAME}_test APPEND PROPERTY COMPILE_DEFINITIONS
                 ANJAY_TEST
                 "ANJAY_BIN_DIR=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\"")
    set_property(TARGET ${NAME}_test APPEND PROPERTY COMPILE_FLAGS
                 "-Wno-pedantic -Wno-overlength-strings")

    if(VALGRIND)
        set(VALGRIND_LOG ${VALGRIND} --log-file=VALGRIND.${NAME}.log)
    else()
        set(VALGRIND_LOG)
    endif()

    add_test(NAME ${NAME}_test
             WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
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
    add_test(NAME test_${F}_visibility COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_visibility.sh ${F})
endforeach()

add_custom_target(visibility_check COMMAND ${CMAKE_CTEST_COMMAND} -R "'^test_.*_visibility$$'")
add_dependencies(anjay_unit_check visibility_check)

set(ABSOLUTE_HEADERS)
foreach(F ${ABSOLUTE_SOURCES})
    if(F MATCHES [.]h$)
        list(APPEND ABSOLUTE_HEADERS ${F})
    endif()
endforeach()
add_test(NAME test_function_duplicates COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_duplicates.py ${ABSOLUTE_HEADERS})

add_custom_target(function_duplicates_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_function_duplicates$$'")
add_custom_target(examples_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_examples$$'")
add_dependencies(anjay_unit_check function_duplicates_check)

################# SYMBOL VALIDATION ############################################

add_custom_target(symbols_check COMMAND ${CMAKE_CTEST_COMMAND} -R "'^test_.*_symbols$$'" --output-on-failure)

if(WITH_LIBRARY_SHARED)
    add_test(NAME test_so_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_symbols.sh $<TARGET_FILE:${PROJECT_NAME}> anjay_ ANJAY_)
    add_dependencies(symbols_check ${PROJECT_NAME})
endif()

add_test(NAME test_a_symbols COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_symbols.sh $<TARGET_FILE:${PROJECT_NAME}_static> anjay_ ANJAY_ _anjay_ _ANJAY_)
add_dependencies(symbols_check ${PROJECT_NAME}_static)

add_dependencies(anjay_unit_check symbols_check)

################# UNIT TESTS ###################################################

if(WITH_VALGRIND)
    set(VALGRIND ${VALGRIND_EXECUTABLE} --leak-check=full --track-origins=yes -q --error-exitcode=63 --suppressions=${CMAKE_CURRENT_SOURCE_DIR}/valgrind_test.supp)
endif()

if(WITH_AVS_UNIT)
    include_directories(test/include)
    add_anjay_test(${PROJECT_NAME} ${ABSOLUTE_TEST_SOURCES})
    add_dependencies(${PROJECT_NAME}_test dtls_echo_server)
    target_link_libraries(${PROJECT_NAME}_test ${DEPS_LIBRARIES} ${DEPS_LIBRARIES_WEAK})
endif()

option(WITH_INTEGRATION_TESTS "Enable integration tests" OFF)
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
                          COMMAND "${AFL_FUZZER_DIR}/afl-fuzz" -i "${FUZZ_TEST_CASE_DIR}"
                                                               -o "${FUZZ_TEST_FINDINGS_DIR}"
                                                               -t ${AFL_FUZZ_TIMEOUT_MS}
                                                               "${${FUZZ_TEST_ARG_LIST_NAME}}"
                                                               WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/test/fuzz")
    endforeach()
endif()

################# DOCS #########################################################

add_custom_target(doc)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx/source/conf.py.in")
    find_program(SPHINX_BUILD_EXECUTABLE sphinx-build
                 HINTS $ENV{SPHINX_DIR} PATH_SUFFIXES bin)

    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sphinx.cmake)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx/source/conf.py.in
                   ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx/source/conf.py
                   @ONLY)
    add_custom_target(doc_sphinx
                      COMMAND ${SPHINX_BUILD_EXECUTABLE}
                              -b html
                              ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx/source
                              ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx/build/html
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx)

    add_dependencies(doc doc_sphinx)

    # sphinx-based .. snippet-source:: validation
    add_test(NAME test_examples
             COMMAND ${SPHINX_BUILD_EXECUTABLE}
                     -Q -b snippet_source_lint
                     ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx/source
                     ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx/build/lint
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/doc/sphinx)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
    find_package(Doxygen)

    set(DOXYGEN_INPUT_PATHS ${PUBLIC_INCLUDE_DIRS})
    # doxygen expects whitespace-separated list, cmake stores them as
    # semicolon-separated strings
    string(REPLACE ";" " " DOXYGEN_INPUT_PATHS "${DOXYGEN_INPUT_PATHS}")

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
                   ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
                   @ONLY)

    add_custom_target(doc_doxygen
                      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})


    if(TARGET doc_sphinx)
        add_dependencies(doc_sphinx doc_doxygen)
    endif()
    add_dependencies(doc doc_doxygen)
endif()

################# EXAMPLES #####################################################

set(TUTORIALS_BUILD_DIR "${CMAKE_BINARY_DIR}/examples/build")
set(TUTORIALS_ANJAY_BUILD_DIR "${TUTORIALS_BUILD_DIR}/anjay-build")
set(TUTORIALS_ANJAY_INSTALL_DIR "${TUTORIALS_BUILD_DIR}/anjay")

add_custom_target(examples)
add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${TUTORIALS_BUILD_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${TUTORIALS_ANJAY_INSTALL_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${TUTORIALS_ANJAY_BUILD_DIR}")

add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${CMAKE_CURRENT_SOURCE_DIR}
                        -B.
                        -DCMAKE_INSTALL_PREFIX="${TUTORIALS_ANJAY_INSTALL_DIR}"
                   COMMAND ${CMAKE_COMMAND} --build . --target install -- -j$(nproc)
                   WORKING_DIRECTORY ${TUTORIALS_ANJAY_BUILD_DIR})

add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${CMAKE_CURRENT_SOURCE_DIR}/examples/tutorial/
                        -B.
                        -DCMAKE_PREFIX_PATH="${TUTORIALS_ANJAY_INSTALL_DIR}"
                        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/examples"
                   COMMAND ${CMAKE_COMMAND} --build . -- -j$(nproc)
                   WORKING_DIRECTORY "${TUTORIALS_BUILD_DIR}")

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
               ${CMAKE_CURRENT_BINARY_DIR}/cmake/${PROJECT_NAME}-config.cmake
               @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/anjay-version.cmake.in
               ${CMAKE_CURRENT_BINARY_DIR}/cmake/${PROJECT_NAME}-version.cmake
               @ONLY)

install(EXPORT ${PROJECT_NAME}-targets DESTINATION ${LIB_INSTALL_DIR}/${PROJECT_NAME})
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/${PROJECT_NAME}-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/${PROJECT_NAME}-version.cmake
        DESTINATION ${LIB_INSTALL_DIR}/${PROJECT_NAME})
