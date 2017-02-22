cmake_minimum_required(VERSION 3.1)
project(event-loop C)

find_package(anjay REQUIRED)

set(TUTORIAL_SOURCES
    src/main.c)

include_directories(${ANJAY_INCLUDE_DIRS})

add_executable(${PROJECT_NAME} ${TUTORIAL_SOURCES})
set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 99)

target_link_libraries(${PROJECT_NAME} ${ANJAY_LIBRARIES_STATIC})
