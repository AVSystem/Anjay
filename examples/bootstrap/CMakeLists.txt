cmake_minimum_required(VERSION 3.4.0)

project(bootstrap_example C)

set(ANJAY_WITH_EXTRA_WARNINGS ON)
set(ANJ_WITH_SDM_LOGS ON)
set(ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER 1)
set(ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER 2)

set(anjay_DIR "../../cmake")
find_package(anjay 4 REQUIRED)

add_executable(bootstrap_example main.c bootstrap_event_loop.c ../compat/anj_net_impl.c ../compat/net_integration.c)

target_include_directories(bootstrap_example PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/../compat"
    )

target_link_libraries(bootstrap_example PRIVATE anj mbedtls mbedcrypto anl)
