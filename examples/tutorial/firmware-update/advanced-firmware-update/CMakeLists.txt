cmake_minimum_required(VERSION 3.1)
project(advanced-firmware-update C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)

find_package(anjay REQUIRED)

add_executable(${PROJECT_NAME}
               src/main.c
               src/advanced_firmware_update.c
               src/advanced_firmware_update.h
               src/time_object.c
               src/time_object.h)
target_link_libraries(${PROJECT_NAME} PRIVATE anjay)
