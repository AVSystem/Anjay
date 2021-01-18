# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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


add_custom_target(examples)

# TUTORIALS

set(ANJAY_TUTORIALS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/tutorial-build")
set(ANJAY_TUTORIALS_ANJAY_BUILD_DIR "${ANJAY_TUTORIALS_BUILD_DIR}/anjay-build")
set(ANJAY_TUTORIALS_ANJAY_INSTALL_DIR "${ANJAY_TUTORIALS_BUILD_DIR}/anjay")

add_custom_target(tutorial_examples)
add_custom_command(TARGET tutorial_examples
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_TUTORIALS_BUILD_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_TUTORIALS_ANJAY_BUILD_DIR}")

add_custom_command(TARGET tutorial_examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${ANJAY_SOURCE_DIR}
                        -B.
                        -DCMAKE_INSTALL_PREFIX="${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                        -DWITH_LIBRARY_SHARED=OFF
                        -DWITH_DEMO=OFF
                        -DCMAKE_BUILD_TYPE=Debug
                   COMMAND ${CMAKE_COMMAND} --build . --target install -- -j${NPROC}
                   WORKING_DIRECTORY ${ANJAY_TUTORIALS_ANJAY_BUILD_DIR})

add_custom_command(TARGET tutorial_examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${CMAKE_CURRENT_SOURCE_DIR}/tutorial/
                        -B.
                        -DCMAKE_PREFIX_PATH="${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/examples"
                        -DCMAKE_BUILD_TYPE=Debug
                   COMMAND ${CMAKE_COMMAND} --build . -- -j${NPROC}
                   WORKING_DIRECTORY "${ANJAY_TUTORIALS_BUILD_DIR}")

add_dependencies(examples tutorial_examples)

# CUSTOM NETWORK LAYER TUTORIALS

set(ANJAY_CUSTOM_NETWORK_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/custom-network-build")
set(ANJAY_CUSTOM_NETWORK_ANJAY_BUILD_DIR "${ANJAY_CUSTOM_NETWORK_BUILD_DIR}/anjay-build")
set(ANJAY_CUSTOM_NETWORK_ANJAY_INSTALL_DIR "${ANJAY_CUSTOM_NETWORK_BUILD_DIR}/anjay")

add_custom_target(custom_network_examples)
add_custom_command(TARGET custom_network_examples
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_CUSTOM_NETWORK_BUILD_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_CUSTOM_NETWORK_ANJAY_INSTALL_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_CUSTOM_NETWORK_ANJAY_BUILD_DIR}")

add_custom_command(TARGET custom_network_examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${ANJAY_SOURCE_DIR}
                        -B.
                        -DCMAKE_INSTALL_PREFIX="${ANJAY_CUSTOM_NETWORK_ANJAY_INSTALL_DIR}"
                        -DWITH_LIBRARY_SHARED=OFF
                        -DWITH_DEMO=OFF
                        -DWITH_POSIX_AVS_SOCKET=OFF
                        -DWITHOUT_IP_STICKINESS=ON
                        -DCMAKE_BUILD_TYPE=Debug
                   COMMAND ${CMAKE_COMMAND} --build . --target install -- -j${NPROC}
                   WORKING_DIRECTORY ${ANJAY_CUSTOM_NETWORK_ANJAY_BUILD_DIR})

add_custom_command(TARGET custom_network_examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${CMAKE_CURRENT_SOURCE_DIR}/custom-network/
                        -B.
                        -DCMAKE_PREFIX_PATH="${ANJAY_CUSTOM_NETWORK_ANJAY_INSTALL_DIR}"
                        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/examples"
                        -DCMAKE_BUILD_TYPE=Debug
                   COMMAND ${CMAKE_COMMAND} --build . -- -j${NPROC}
                   WORKING_DIRECTORY "${ANJAY_CUSTOM_NETWORK_BUILD_DIR}")

add_dependencies(examples custom_network_examples)

# ip-stickiness example

set(ANJAY_CUSTOM_NETWORK_IP_STICKINESS_BUILD_DIR "${ANJAY_CUSTOM_NETWORK_BUILD_DIR}/ip-stickiness")
set(ANJAY_CUSTOM_NETWORK_IP_STICKINESS_ANJAY_BUILD_DIR "${ANJAY_CUSTOM_NETWORK_BUILD_DIR}/anjay-with-ip-stickiness-build")
set(ANJAY_CUSTOM_NETWORK_IP_STICKINESS_ANJAY_INSTALL_DIR "${ANJAY_CUSTOM_NETWORK_BUILD_DIR}/anjay-with-ip-stickiness")

add_custom_target(custom_network_ip_stickiness_example)
add_custom_command(TARGET custom_network_ip_stickiness_example
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_CUSTOM_NETWORK_IP_STICKINESS_BUILD_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_CUSTOM_NETWORK_IP_STICKINESS_ANJAY_BUILD_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_CUSTOM_NETWORK_IP_STICKINESS_ANJAY_INSTALL_DIR}")

add_custom_command(TARGET custom_network_ip_stickiness_example
                   COMMAND ${CMAKE_COMMAND}
                        -H${ANJAY_SOURCE_DIR}
                        -B.
                        -DCMAKE_INSTALL_PREFIX="${ANJAY_CUSTOM_NETWORK_IP_STICKINESS_ANJAY_INSTALL_DIR}"
                        -DWITH_LIBRARY_SHARED=OFF
                        -DWITH_DEMO=OFF
                        -DWITH_POSIX_AVS_SOCKET=OFF
                        -DCMAKE_BUILD_TYPE=Debug
                   COMMAND ${CMAKE_COMMAND} --build . --target install -- -j${NPROC}
                   WORKING_DIRECTORY ${ANJAY_CUSTOM_NETWORK_IP_STICKINESS_ANJAY_BUILD_DIR})

add_custom_command(TARGET custom_network_ip_stickiness_example
                   COMMAND ${CMAKE_COMMAND}
                        -H${CMAKE_CURRENT_SOURCE_DIR}/custom-network/ip-stickiness
                        -B.
                        -DCMAKE_PREFIX_PATH="${ANJAY_CUSTOM_NETWORK_IP_STICKINESS_ANJAY_INSTALL_DIR}"
                        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/examples"
                        -DCMAKE_BUILD_TYPE=Debug
                   COMMAND ${CMAKE_COMMAND} --build . -- -j${NPROC}
                   WORKING_DIRECTORY "${ANJAY_CUSTOM_NETWORK_IP_STICKINESS_BUILD_DIR}")

add_dependencies(custom_network_examples custom_network_ip_stickiness_example)

if(TARGET check)
    add_dependencies(check examples)
endif()
