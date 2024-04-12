cmake_minimum_required(VERSION 3.4.0)

project(dm_tests C)

set(ANJAY_WITH_EXTRA_WARNINGS ON)
set(ANJAY_TESTING ON)
set(WITH_DDM ON)
set(DM_WITH_LOGS OFF)

set(anjay_DIR "../../../cmake")
find_package(anjay 4 REQUIRED)

file(GLOB dm_tests_sources "*.c")

add_executable(dm_tests ${dm_tests_sources})

target_compile_options(dm_tests PRIVATE -DUNIT_TESTING -Wno-pedantic
                                        -Wno-c++-compat)
target_link_libraries(dm_tests PRIVATE anj)
