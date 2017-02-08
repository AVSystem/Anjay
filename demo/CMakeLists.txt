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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ssl_key.h")
    set(DEMO_SSL_KEY_FILE ssl_key.h)
    set(HEADERS ${HEADERS} ${DEMO_SSL_KEY_FILE})
endif()

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

if(DEMO_SSL_KEY_FILE)
    set_property(TARGET demo APPEND PROPERTY COMPILE_DEFINITIONS
                 "DEMO_SSL_KEY_FILE=\"${DEMO_SSL_KEY_FILE}\"")
endif()

add_custom_target(demo_firmware
                  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../test/integration/framework/firmware_package.py
                          -i ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/demo
                          -o ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/demo.fw-pkg
                  DEPENDS demo)

