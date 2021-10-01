cmake_minimum_required(VERSION 3.1)
project(anjay-bc-thread-safety C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)

add_compile_options(-Wall -Wextra)

find_package(anjay REQUIRED)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME}
               src/main.c
               src/time_object.h
               src/time_object.c)
target_link_libraries(${PROJECT_NAME} PRIVATE anjay ${CMAKE_THREAD_LIBS_INIT})
