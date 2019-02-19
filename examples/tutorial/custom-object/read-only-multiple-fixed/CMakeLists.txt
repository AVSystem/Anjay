cmake_minimum_required(VERSION 2.8)
project(multi-instance-read-only-fixed C)

if(NOT CMAKE_VERSION VERSION_LESS 3.1)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_EXTENSIONS OFF)
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
endif()

find_package(anjay REQUIRED)

set(SOURCES src/main.c)

include_directories(${ANJAY_INCLUDE_DIRS})

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} ${ANJAY_LIBRARIES_STATIC})
