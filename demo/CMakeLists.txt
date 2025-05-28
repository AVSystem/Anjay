# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

cmake_minimum_required(VERSION 3.16)
project(lwm2m_demo C)

option(WITH_DEMO_USE_STANDALONE_OBJECTS "Use standalone versions of built-in objects in demo" OFF)

set(SOURCES
    demo.c
    demo_args.c
    demo_cmds.c
    demo_utils.c
    demo_time.c
    objects/apn_conn_profile.c
    objects/binary_app_data_container.c
    objects/cell_connectivity.c
    objects/conn_monitoring.c
    objects/conn_statistics.c
    objects/device.c
    objects/download_diagnostics.c
    objects/event_log.c
    objects/ext_dev_info.c
    objects/geopoints.c
    objects/ipso_objects.c
    objects/location.c
    objects/portfolio.c
    objects/test.c)

if(WITH_DEMO_USE_STANDALONE_OBJECTS)
    file(GLOB STANDALONE_SOURCES ../standalone/*/*.c)
    set(SOURCES ${SOURCES} ${STANDALONE_SOURCES})
endif()

if (${ANJAY_WITH_MODULE_FW_UPDATE})
    set(SOURCES ${SOURCES} firmware_update.c)
endif()


if (${ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE})
    set(SOURCES ${SOURCES}
                advanced_firmware_update.c
                advanced_firmware_update_app.c
                advanced_firmware_update_addimg.c)

endif()

if (${ANJAY_WITH_MODULE_SW_MGMT})
    set(SOURCES ${SOURCES} software_mgmt.c)
endif()

if (${ANJAY_WITH_LWM2M_GATEWAY})
    set(SOURCES ${SOURCES} lwm2m_gateway.c
                           objects/gateway_end_devices/temperature_object.c
                           objects/gateway_end_devices/push_button_object.c
                           objects/gateway_end_devices/binary_app_data_container.c
                           )
endif()

if(NOT WIN32)
    set(SOURCES ${SOURCES} objects/ip_ping.c)
endif()

set(HEADERS
    demo.h
    demo_args.h
    demo_cmds.h
    demo_utils.h
    objects.h)

if(WITH_DEMO_USE_STANDALONE_OBJECTS)
    file(GLOB STANDALONE_HEADERS ../standalone/*/*.h)
    set(HEADERS ${HEADERS} ${STANDALONE_HEADERS})
endif()

if (${ANJAY_WITH_MODULE_FW_UPDATE})
    set(HEADERS ${HEADERS} firmware_update.h)
endif()

if (${ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE})
    set(HEADERS ${HEADERS} advanced_firmware_update.h)
endif()

if (${ANJAY_WITH_MODULE_SW_MGMT})
    set(HEADERS ${HEADERS} software_mgmt.h)
endif()

set(ALL_SOURCES ${SOURCES} ${HEADERS})

if(NOT TARGET anjay)
    find_package(anjay REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

find_package(Threads REQUIRED)

add_executable(demo ${ALL_SOURCES})
target_link_libraries(demo PRIVATE anjay m ${CMAKE_THREAD_LIBS_INIT})

if(WITH_DEMO_USE_STANDALONE_OBJECTS)
    target_compile_definitions(demo PRIVATE WITH_DEMO_USE_STANDALONE_OBJECTS)
endif()


add_custom_target(demo_firmware
                  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tests/integration/framework/create_package.py
                          -i ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/demo
                          -o ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/demo.fw-pkg
                  DEPENDS demo)
