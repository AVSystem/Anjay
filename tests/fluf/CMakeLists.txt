cmake_minimum_required(VERSION 3.4.0)

project(fluf_tests C)

set(ANJAY_WITH_EXTRA_WARNINGS ON)
set(ANJAY_TESTING ON)

set(anjay_DIR "../../cmake")
find_package(anjay 4 REQUIRED)

file(GLOB fluf_tests_sources "*.c")

add_executable(fluf_tests ${fluf_tests_sources})
target_link_libraries(fluf_tests PRIVATE fluf)
