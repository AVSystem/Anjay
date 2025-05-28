cmake_minimum_required(VERSION 3.16)
project(anjay-lwm2m-gateway C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra)

find_package(anjay REQUIRED)

add_executable(${PROJECT_NAME}
               src/main.c
               src/gateway_server.h
               src/gateway_server.c
               src/temperature_object.h
               src/temperature_object.c)
target_link_libraries(${PROJECT_NAME} PRIVATE anjay)
