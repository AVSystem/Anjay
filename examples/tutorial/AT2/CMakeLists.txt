cmake_minimum_required(VERSION 3.1)
project(at2-anjay-tutorial C)

find_package(anjay REQUIRED)

set(TUTORIAL_SOURCES
    src/main.c)

include_directories(${ANJAY_INCLUDE_DIRS})

add_executable(at2-anjay-tutorial ${TUTORIAL_SOURCES})
set_property(TARGET at2-anjay-tutorial PROPERTY C_STANDARD 99)

target_link_libraries(at2-anjay-tutorial ${ANJAY_LIBRARIES_STATIC})
