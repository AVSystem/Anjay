# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

cmake_minimum_required(VERSION 3.16)
project(probe_crypto_engine_backend C)

include(FindPkgConfig)
include(
    "${CMAKE_CURRENT_LIST_DIR}/../../deps/avs_commons/cmake/CheckMbedTlsPsaCrypto.cmake")

# This tiny configure-only project is called from devconfig.
# Its job is simple: inspect the currently selected TLS backend and write one
# backend name to backend.txt, so bash does not need to duplicate CMake logic.
#
# Result values:
# - pkcs11: a PKCS#11-backed engine is available
# - psa:    a PSA-backed engine is available
# - none:   no usable engine backend was detected
set(DETECTED_CRYPTO_ENGINE_BACKEND none)
set(DETECTED_CRYPTO_ENGINE_BACKEND_FILE
    "${CMAKE_BINARY_DIR}/backend.txt"
    CACHE FILEPATH
    "")

if(DTLS_BACKEND STREQUAL "mbedtls")
    list(APPEND CMAKE_MODULE_PATH
         "${CMAKE_CURRENT_LIST_DIR}/../../deps/avs_commons/cmake")

    # Respect explicit backend flags first. If the caller already selected a
    # backend, the probe should report that choice instead of second-guessing
    # it.
    if(WITH_MBEDTLS_PKCS11_ENGINE)
        set(DETECTED_CRYPTO_ENGINE_BACKEND pkcs11)
    elseif(WITH_MBEDTLS_PSA_ENGINE)
        set(DETECTED_CRYPTO_ENGINE_BACKEND psa)
    else()
        # For the default system-library path, inspect the discovered Mbed TLS:
        # - 2.x can use the legacy PKCS#11 backend
        # - 3.x can use PSA only if the selected config enables it
        find_package(MbedTLS QUIET)
        if(MBEDTLS_FOUND)
            if(MBEDTLS_VERSION VERSION_LESS 3.0.0)
                set(DETECTED_CRYPTO_ENGINE_BACKEND pkcs11)
            else()
                check_mbedtls_psa_crypto_enabled(MBEDTLS_HAS_PSA_CRYPTO)
                if(MBEDTLS_HAS_PSA_CRYPTO)
                    set(DETECTED_CRYPTO_ENGINE_BACKEND psa)
                endif()
            endif()
        endif()
    endif()
elseif(DTLS_BACKEND STREQUAL "openssl")
    # OpenSSL engine support always uses PKCS#11. Prefer the explicit flag, and
    # otherwise check whether libp11 is available on the system.
    if(WITH_OPENSSL_PKCS11_ENGINE)
        set(DETECTED_CRYPTO_ENGINE_BACKEND pkcs11)
    else()
        pkg_search_module(LIBP11 QUIET libp11)
        if(LIBP11_FOUND)
            set(DETECTED_CRYPTO_ENGINE_BACKEND pkcs11)
        endif()
    endif()
endif()

# Return the decision to devconfig through tiny text file, so bash does not
# need to understand Mbed TLS/OpenSSL feature checks on its own.
file(WRITE "${DETECTED_CRYPTO_ENGINE_BACKEND_FILE}"
     "${DETECTED_CRYPTO_ENGINE_BACKEND}")
