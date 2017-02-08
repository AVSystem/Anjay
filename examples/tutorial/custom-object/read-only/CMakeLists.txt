cmake_minimum_required(VERSION 3.1)
project(single-instance-read-only C)

find_package(anjay REQUIRED)

set(SOURCES src/main.c)

include_directories(${ANJAY_INCLUDE_DIRS})

add_executable(${PROJECT_NAME} ${SOURCES})
set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 99)

target_link_libraries(${PROJECT_NAME} ${ANJAY_LIBRARIES_STATIC})
