# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED PY_BUILD_CMAKE_VERSION)
    message(FATAL_ERROR
        "This CMake project is intended to be built via py-build-cmake"
    )
endif()

if(NOT PY_BUILD_CMAKE_IMPORT_NAME)
    message(FATAL_ERROR "PY_BUILD_CMAKE_IMPORT_NAME not set by py-build-cmake")
endif()

project(pymbedtls CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB sources "src/*.cpp")
file(GLOB headers "src/*.hpp")

include(cmake/QueryPythonForPybind11.cmake)
find_pybind11_python_first()

pybind11_add_module(pymbedtls MODULE ${sources} ${headers})

# Avoid polluting the exported symbols
set_target_properties(pymbedtls PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

# Pass env MBEDTLS_ROOT_DIR into the CMake variable expected by FindMbedTLS.cmake
if(NOT DEFINED MBEDTLS_ROOT_DIR AND DEFINED ENV{MBEDTLS_ROOT_DIR})
    set(MBEDTLS_ROOT_DIR "$ENV{MBEDTLS_ROOT_DIR}")
endif()

# Prepend CMake module path to find FindMbedTLS.cmake
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(MbedTLS REQUIRED)
target_link_libraries(pymbedtls PRIVATE mbedtls mbedx509 mbedcrypto)

# If using a specific build of mbedTLS, bake in a hint for runtime linker that
# the mbedTLS shared libraries can be found in MBEDTLS_ROOT_DIR/lib. This avoids
# the need to set LD_LIBRARY_PATH at runtime.
if(MBEDTLS_ROOT_DIR)
    set(_mbedtls_libdir "${MBEDTLS_ROOT_DIR}/lib")
    set_target_properties(pymbedtls PROPERTIES
        BUILD_RPATH "${_mbedtls_libdir}"
        INSTALL_RPATH "${_mbedtls_libdir}"
    )
endif()

install(TARGETS pymbedtls
        DESTINATION ${PY_BUILD_CMAKE_IMPORT_NAME})
