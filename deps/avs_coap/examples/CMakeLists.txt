# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem CoAP library
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

set(EXAMPLES_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/build")
set(AVS_COAP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(AVS_COAP_BUILD_DIR "${EXAMPLES_BUILD_DIR}/avs_coap-build")
set(AVS_COAP_INSTALL_DIR "${EXAMPLES_BUILD_DIR}/avs_coap")

add_custom_target(avs_coap_examples_install)
add_custom_command(TARGET avs_coap_examples_install
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${EXAMPLES_BUILD_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${AVS_COAP_INSTALL_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${AVS_COAP_BUILD_DIR}")

add_custom_command(TARGET avs_coap_examples_install
                   COMMAND ${CMAKE_COMMAND}
                        -H${AVS_COAP_SOURCE_DIR}
                        -B.
                        -DCMAKE_INSTALL_PREFIX="${AVS_COAP_INSTALL_DIR}"
                   COMMAND ${CMAKE_COMMAND} --build . --target install -- -j${NPROC}
                   WORKING_DIRECTORY ${AVS_COAP_BUILD_DIR})

add_custom_target(avs_coap_examples)
if(TARGET avs_coap_check)
    add_dependencies(avs_coap_check avs_coap_examples)
endif()

function(add_example NAME)
    set(TARGET_NAME avs_coap_example-${NAME})
    set(BUILD_DIR "${EXAMPLES_BUILD_DIR}/${NAME}")

    add_custom_target(${TARGET_NAME})
    add_custom_command(TARGET ${TARGET_NAME}
                       COMMAND ${CMAKE_COMMAND} -E make_directory "${BUILD_DIR}")
    add_custom_command(TARGET ${TARGET_NAME}
                       COMMAND ${CMAKE_COMMAND}
                            -H${CMAKE_CURRENT_SOURCE_DIR}/${NAME}/
                            -B.
                            -DCMAKE_PREFIX_PATH="${AVS_COAP_INSTALL_DIR}"
                       COMMAND ${CMAKE_COMMAND} --build . -- VERBOSE=1 -j${NPROC}
                       WORKING_DIRECTORY "${BUILD_DIR}")

    add_dependencies(${TARGET_NAME} avs_coap_examples_install)
    add_dependencies(avs_coap_examples ${TARGET_NAME})
endfunction()

add_example(async-client)
