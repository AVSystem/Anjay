cmake_minimum_required(VERSION 3.16)
project(anjay-pkcs11 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)

find_package(anjay REQUIRED)

# check if AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE is enabled
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/avs_commons_check.c
     "#include <avsystem/commons/avs_commons_config.h>\nint main() {\n#ifndef AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE\nint error[-1];\n#endif\nreturn 0; }\n")
get_target_property(INCLUDE_DIRS anjay INTERFACE_INCLUDE_DIRECTORIES)
try_compile(WITH_AVS_CRYPTO_PKI_ENGINE
            ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp
            ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/CMakeTmp/avs_commons_check.c
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${INCLUDE_DIRS}")

if(WITH_AVS_CRYPTO_PKI_ENGINE)
    add_executable(${PROJECT_NAME} src/main.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE anjay)
endif()
