cmake_minimum_required(VERSION 3.1)
project(multi-instance-dynamic C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)

find_package(anjay REQUIRED)

add_executable(${PROJECT_NAME}
               src/main.c
               src/test_object.c
               src/test_object.h)
target_link_libraries(${PROJECT_NAME} PRIVATE anjay)
