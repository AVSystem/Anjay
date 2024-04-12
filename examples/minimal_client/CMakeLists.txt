cmake_minimum_required(VERSION 3.4.0)

project(minimal_client_example C)

set(ANJAY_WITH_EXTRA_WARNINGS ON)
set(ANJ_WITH_SDM_LOGS ON)
set(ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER 1)
set(ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER 1)

set(anjay_DIR "../../cmake")
find_package(anjay 4 REQUIRED)

add_executable(minimal_client_example main.c ../compat/event_loop.c ../compat/anj_net_impl.c ../compat/net_integration.c)

target_include_directories(minimal_client_example PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/../compat"
    )

target_link_libraries(minimal_client_example PRIVATE anj mbedtls mbedcrypto anl)
