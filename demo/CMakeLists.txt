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

cmake_minimum_required(VERSION 2.8.1)
project(lwm2m_demo C)

set(SOURCES
    demo.c
    demo_args.c
    demo_cmds.c
    iosched.c
    utils.c
    wget_downloader.c
    objects/apn_conn_profile.c
    objects/cell_connectivity.c
    objects/conn_monitoring.c
    objects/conn_statistics.c
    objects/download_diagnostics.c
    objects/device.c
    objects/ext_dev_info.c
    objects/firmware_update.c
    objects/geopoints.c
    objects/ip_ping.c
    objects/location.c
    objects/test.c)

set(HEADERS
    iosched.h
    objects.h
    utils.h
    wget_downloader.h)

set(ALL_SOURCES ${SOURCES} ${HEADERS})

if(NOT ANJAY_INCLUDE_DIRS AND NOT ANJAY_LIBRARIES)
    find_package(anjay REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

find_program(WGET_EXECUTABLE wget)
if(NOT WGET_EXECUTABLE)
    message(FATAL_ERROR "wget executable is necessary for the demo application")
endif()

if(TARGET ${ANJAY_LIBRARIES_STATIC})
    set(DEMO_ANJAY_TARGET "${ANJAY_LIBRARIES_STATIC}")
else()
    set(DEMO_ANJAY_TARGET "${ANJAY_LIBRARIES}")
endif()

include_directories(${ANJAY_INCLUDE_DIRS})

add_executable(demo ${ALL_SOURCES})
target_link_libraries(demo ${DEMO_ANJAY_TARGET} m)

add_custom_target(demo_firmware
                  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../test/integration/framework/firmware_package.py
                          -i ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/demo
                          -o ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/demo.fw-pkg
                  DEPENDS demo)

