# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

# NOTE:
# This is extracted from avs_commons. Long-term it'd be preferable to assume
# that the system has mbedTLS installed and use CMake config mode instead, but
# for now let's use the exact same logic as in avs_commons to avoid
# discrepancies.

#.rst:
# FindMbedTLS
# -----------
#
# Find the mbedTLS encryption library.
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines the following :prop_tgt:`IMPORTED` targets:
#
# ``mbedtls``
#   The mbedTLS ``mbedtls`` library, if found.
# ``mbedcrypto``
#   The mbedtls ``crypto`` library, if found.
# ``mbedx509``
#   The mbedtls ``x509`` library, if found.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module will set the following variables in your project:
#
# ``MBEDTLS_FOUND``
#   System has the mbedTLS library.
# ``MBEDTLS_INCLUDE_DIR``
#   The mbedTLS include directory.
# ``MBEDTLS_LIBRARY``
#   The mbedTLS SSL library.
# ``MBEDTLS_CRYPTO_LIBRARY``
#   The mbedTLS crypto library.
# ``MBEDTLS_X509_LIBRARY``
#   The mbedTLS x509 library.
# ``MBEDTLS_LIBRARIES``
#   All mbedTLS libraries.
# ``MBEDTLS_VERSION``
#   This is set to ``$major.$minor.$patch``.
# ``MBEDTLS_VERSION_MAJOR``
#   Set to major mbedTLS version number.
# ``MBEDTLS_VERSION_MINOR``
#   Set to minor mbedTLS version number.
# ``MBEDTLS_VERSION_PATCH``
#   Set to patch mbedTLS version number.
#
# Hints
# ^^^^^
#
# Set ``MBEDTLS_ROOT_DIR`` to the root directory of an mbedTLS installation.
# Set ``MBEDTLS_USE_STATIC_LIBS`` to ``TRUE`` to look for static libraries.

set(_ORIG_FIND_ROOT_PATH_MODE_INCLUDE "${CMAKE_FIND_ROOT_PATH_MODE_INCLUDE}")
set(_ORIG_FIND_ROOT_PATH_MODE_LIBRARY "${CMAKE_FIND_ROOT_PATH_MODE_LIBRARY}")

if(MBEDTLS_ROOT_DIR)
    # Disable re-rooting paths in find_path/find_library.
    # This assumes MBEDTLS_ROOT_DIR is an absolute path.
    set(_EXTRA_FIND_ARGS "NO_DEFAULT_PATH")
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
endif()

find_path(MBEDTLS_INCLUDE_DIR
          NAMES mbedtls/ssl.h
          PATH_SUFFIXES include
          HINTS ${MBEDTLS_ROOT_DIR}
          ${_EXTRA_FIND_ARGS})

# based on https://github.com/ARMmbed/mbedtls/issues/298
set(MBEDTLS_BUILD_INFO_FILE)
if(MBEDTLS_INCLUDE_DIR)
    if(EXISTS "${MBEDTLS_INCLUDE_DIR}/mbedtls/build_info.h")
        # Mbed TLS 3.x
        set(MBEDTLS_BUILD_INFO_FILE "${MBEDTLS_INCLUDE_DIR}/mbedtls/build_info.h")
    elseif(EXISTS "${MBEDTLS_INCLUDE_DIR}/mbedtls/version.h")
        # Mbed TLS 2.x
        set(MBEDTLS_BUILD_INFO_FILE "${MBEDTLS_INCLUDE_DIR}/mbedtls/version.h")
    endif()
endif()

if(MBEDTLS_BUILD_INFO_FILE)
    file(STRINGS "${MBEDTLS_BUILD_INFO_FILE}" VERSION_STRING_LINE REGEX "^#define MBEDTLS_VERSION_STRING[ \\t\\n\\r]+\"[^\"]*\"$")
    file(STRINGS "${MBEDTLS_BUILD_INFO_FILE}" VERSION_MAJOR_LINE REGEX "^#define MBEDTLS_VERSION_MAJOR[ \\t\\n\\r]+[0-9]+$")
    file(STRINGS "${MBEDTLS_BUILD_INFO_FILE}" VERSION_MINOR_LINE REGEX "^#define MBEDTLS_VERSION_MINOR[ \\t\\n\\r]+[0-9]+$")
    file(STRINGS "${MBEDTLS_BUILD_INFO_FILE}" VERSION_PATCH_LINE REGEX "^#define MBEDTLS_VERSION_PATCH[ \\t\\n\\r]+[0-9]+$")

    string(REGEX REPLACE "^#define MBEDTLS_VERSION_STRING[ \\t\\n\\r]+\"([^\"]*)\"$" "\\1" MBEDTLS_VERSION "${VERSION_STRING_LINE}")
    string(REGEX REPLACE "^#define MBEDTLS_VERSION_MAJOR[ \\t\\n\\r]+([0-9]+)$" "\\1" MBEDTLS_VERSION_MAJOR "${VERSION_MAJOR_LINE}")
    string(REGEX REPLACE "^#define MBEDTLS_VERSION_MINOR[ \\t\\n\\r]+([0-9]+)$" "\\1" MBEDTLS_VERSION_MINOR "${VERSION_MINOR_LINE}")
    string(REGEX REPLACE "^#define MBEDTLS_VERSION_PATCH[ \\t\\n\\r]+([0-9]+)$" "\\1" MBEDTLS_VERSION_PATCH "${VERSION_PATCH_LINE}")
endif()


if(MBEDTLS_USE_STATIC_LIBS)
    set(_MBEDTLS_LIB_NAME libmbedtls.a)
    set(_MBEDTLS_CRYPTO_LIB_NAME libmbedcrypto.a)
    set(_MBEDTLS_X509_LIB_NAME libmbedx509.a)
else()
    set(_MBEDTLS_LIB_NAME mbedtls)
    set(_MBEDTLS_CRYPTO_LIB_NAME mbedcrypto)
    set(_MBEDTLS_X509_LIB_NAME mbedx509)
endif()

find_library(MBEDTLS_LIBRARY
             NAMES ${_MBEDTLS_LIB_NAME}
             PATH_SUFFIXES lib
             HINTS ${MBEDTLS_ROOT_DIR}
             ${_EXTRA_FIND_ARGS})

find_library(MBEDTLS_CRYPTO_LIBRARY
             NAMES ${_MBEDTLS_CRYPTO_LIB_NAME}
             PATH_SUFFIXES lib
             HINTS ${MBEDTLS_ROOT_DIR}
             ${_EXTRA_FIND_ARGS})

find_library(MBEDTLS_X509_LIBRARY
             NAMES ${_MBEDTLS_X509_LIB_NAME}
             PATH_SUFFIXES lib
             HINTS ${MBEDTLS_ROOT_DIR}
             ${_EXTRA_FIND_ARGS})

set(MBEDTLS_LIBRARIES ${MBEDTLS_LIBRARY} ${MBEDTLS_CRYPTO_LIBRARY} ${MBEDTLS_X509_LIBRARY})

if(MBEDTLS_INCLUDE_DIR)
    set(MBEDTLS_FOUND TRUE)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE "${_ORIG_FIND_ROOT_PATH_MODE_INCLUDE}")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY "${_ORIG_FIND_ROOT_PATH_MODE_LIBRARY}")


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS
                                  FOUND_VAR MBEDTLS_FOUND
                                  REQUIRED_VARS
                                      MBEDTLS_INCLUDE_DIR
                                      MBEDTLS_LIBRARY
                                      MBEDTLS_CRYPTO_LIBRARY
                                      MBEDTLS_X509_LIBRARY
                                      MBEDTLS_LIBRARIES
                                      MBEDTLS_VERSION
                                  VERSION_VAR MBEDTLS_VERSION)


if(NOT TARGET mbedcrypto)
    add_library(mbedcrypto UNKNOWN IMPORTED)
    set_target_properties(mbedcrypto PROPERTIES
                          INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}"
                          IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                          IMPORTED_LOCATION "${MBEDTLS_CRYPTO_LIBRARY}")
endif()

if(NOT TARGET mbedx509)
    add_library(mbedx509 UNKNOWN IMPORTED)
    set_target_properties(mbedx509 PROPERTIES
                          INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}"
                          INTERFACE_LINK_LIBRARIES mbedcrypto
                          IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                          IMPORTED_LOCATION "${MBEDTLS_X509_LIBRARY}")
endif()

if(NOT TARGET mbedtls)
    add_library(mbedtls UNKNOWN IMPORTED)
    set_target_properties(mbedtls PROPERTIES
                          INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}"
                          INTERFACE_LINK_LIBRARIES mbedx509
                          IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                          IMPORTED_LOCATION "${MBEDTLS_LIBRARY}")
endif()
