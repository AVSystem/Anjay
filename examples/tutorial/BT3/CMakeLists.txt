cmake_minimum_required(VERSION 3.1)
project(anjay-tutorial-bt3 C)

find_package(anjay REQUIRED)

set(TUTORIAL_SOURCES
    src/main.c)

include_directories(${ANJAY_INCLUDE_DIRS})

add_executable(anjay-tutorial-bt3 ${TUTORIAL_SOURCES})
set_property(TARGET anjay-tutorial-bt3 PROPERTY C_STANDARD 99)

target_link_libraries(anjay-tutorial-bt3 ${ANJAY_LIBRARIES_STATIC})
