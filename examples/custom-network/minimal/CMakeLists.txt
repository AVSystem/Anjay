cmake_minimum_required(VERSION 3.1)
project(minimal-custom-network C)

set(CMAKE_C_STANDARD 99)

find_package(anjay REQUIRED)

add_executable(${PROJECT_NAME}
               src/main.c
               src/net_impl.c)
target_link_libraries(${PROJECT_NAME} PRIVATE anjay)
