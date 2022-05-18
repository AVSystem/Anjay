cmake_minimum_required(VERSION 3.1)
project(anjay-nidd C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)

find_package(anjay REQUIRED)

add_executable(${PROJECT_NAME}
                src/main.c
                src/nidd_demo_driver.h
                src/nidd_demo_driver.c)
target_link_libraries(${PROJECT_NAME} PRIVATE anjay)
