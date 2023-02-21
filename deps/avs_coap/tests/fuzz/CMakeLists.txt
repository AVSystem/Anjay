# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem CoAP library
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

if(NOT CMAKE_C_COMPILER MATCHES "/afl-.*$")
    set(FUZZ_TEST_EXCLUDE_FROM_ALL EXCLUDE_FROM_ALL)
endif()

add_executable(avs_coap_parse_stdin ${FUZZ_TEST_EXCLUDE_FROM_ALL}
               coap_parse.c)
target_include_directories(avs_coap_parse_stdin PRIVATE
                           $<TARGET_PROPERTY:avs_coap,INCLUDE_DIRECTORIES>)
target_link_libraries(avs_coap_parse_stdin PRIVATE avs_coap)

add_executable(avs_coap_async_api_udp ${FUZZ_TEST_EXCLUDE_FROM_ALL}
               coap_async_api_udp.c)
target_include_directories(avs_coap_async_api_udp PRIVATE
                           $<TARGET_PROPERTY:avs_coap,INCLUDE_DIRECTORIES>)
target_link_libraries(avs_coap_async_api_udp PRIVATE avs_coap)
add_executable(avs_coap_async_api_tcp ${FUZZ_TEST_EXCLUDE_FROM_ALL}
               coap_async_api_tcp.c)
target_include_directories(avs_coap_async_api_tcp PRIVATE
                           $<TARGET_PROPERTY:avs_coap,INCLUDE_DIRECTORIES>)
target_link_libraries(avs_coap_async_api_tcp PRIVATE avs_coap)

