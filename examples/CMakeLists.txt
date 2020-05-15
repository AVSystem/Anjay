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

set(ANJAY_EXAMPLES_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/build")
set(ANJAY_TUTORIALS_ANJAY_BUILD_DIR "${ANJAY_EXAMPLES_BUILD_DIR}/anjay-build")
set(ANJAY_TUTORIALS_ANJAY_INSTALL_DIR "${ANJAY_EXAMPLES_BUILD_DIR}/anjay")

add_custom_target(examples)
add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_EXAMPLES_BUILD_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${ANJAY_TUTORIALS_ANJAY_BUILD_DIR}")

add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${ANJAY_SOURCE_DIR}
                        -B.
                        -DCMAKE_INSTALL_PREFIX="${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                        -DWITH_LIBRARY_SHARED=OFF
                        -DWITH_DEMO=OFF
                        -DCMAKE_BUILD_TYPE=Debug
                   COMMAND ${CMAKE_COMMAND} --build . --target install -- -j${NPROC}
                   WORKING_DIRECTORY ${ANJAY_TUTORIALS_ANJAY_BUILD_DIR})

add_custom_command(TARGET examples
                   COMMAND ${CMAKE_COMMAND}
                        -H${CMAKE_CURRENT_SOURCE_DIR}/tutorial/
                        -B.
                        -DCMAKE_PREFIX_PATH="${ANJAY_TUTORIALS_ANJAY_INSTALL_DIR}"
                        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/examples"
                        -DCMAKE_BUILD_TYPE=Debug
                   COMMAND ${CMAKE_COMMAND} --build . -- -j${NPROC}
                   WORKING_DIRECTORY "${ANJAY_EXAMPLES_BUILD_DIR}")

if(TARGET check)
    add_dependencies(check examples)
endif()

