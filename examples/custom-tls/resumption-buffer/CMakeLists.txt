cmake_minimum_required(VERSION 3.4)
project(custom-tls-resumption-buffer C)

set(CMAKE_C_STANDARD 99)

find_package(anjay REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(${PROJECT_NAME}
               src/main.c
               src/net_impl.c
               src/tls_impl.c)
target_link_libraries(${PROJECT_NAME} PRIVATE anjay OpenSSL::SSL)
