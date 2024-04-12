cmake_minimum_required(VERSION 3.4.0)

project(anjay)

set(gcc_or_clang
    CMAKE_C_COMPILER_ID
    MATCHES
    "GNU"
    OR
    CMAKE_C_COMPILER_ID
    MATCHES
    "Clang")

macro(define_overridable_option NAME TYPE DEFAULT_VALUE DESCRIPTION)
  if(DEFINED ${NAME})
    set(${NAME}_DEFAULT "${${NAME}}")
  else()
    set(${NAME}_DEFAULT "${DEFAULT_VALUE}")
  endif()
  set(${NAME}
      "${${NAME}_DEFAULT}"
      CACHE ${TYPE} "${DESCRIPTION}")
endmacro()

# Internal options
define_overridable_option(ANJAY_TESTING BOOL OFF "")

# General options
define_overridable_option(ANJAY_WITH_EXTRA_WARNINGS BOOL OFF "")

# avs_commons defaults
define_overridable_option(WITH_AVS_ALGORITHM BOOL ON "")
define_overridable_option(WITH_AVS_UNIT BOOL "${ANJAY_TESTING}" "")
define_overridable_option(WITH_AVS_BUFFER BOOL OFF "")
define_overridable_option(WITH_AVS_LIST BOOL ON "")
define_overridable_option(WITH_AVS_VECTOR BOOL OFF "")
define_overridable_option(WITH_AVS_UTILS BOOL ON "")
define_overridable_option(WITH_AVS_NET BOOL OFF "")
define_overridable_option(WITH_AVS_STREAM BOOL OFF "")
define_overridable_option(WITH_AVS_LOG BOOL ON "")
define_overridable_option(WITH_AVS_RBTREE BOOL OFF "")
define_overridable_option(WITH_AVS_HTTP BOOL OFF "")
define_overridable_option(WITH_AVS_PERSISTENCE BOOL OFF "")
define_overridable_option(WITH_AVS_SCHED BOOL OFF "")
define_overridable_option(WITH_AVS_URL BOOL OFF "")
define_overridable_option(WITH_AVS_COMPAT_THREADING BOOL OFF "")
define_overridable_option(WITH_AVS_CRYPTO BOOL OFF "")
define_overridable_option(WITH_AVS_MICRO_LOGS BOOL OFF "")
define_overridable_option(WITH_AVS_SORTED_SET BOOL OFF "")
define_overridable_option(WITH_INTERNAL_LOGS BOOL ON "")

# FLUF options
define_overridable_option(FLUF_WITH_CBOR BOOL ON "")
define_overridable_option(FLUF_WITH_CBOR_DECIMAL_FRACTIONS BOOL ON "")
define_overridable_option(FLUF_WITH_CBOR_HALF_FLOAT BOOL ON "")
define_overridable_option(FLUF_WITH_CBOR_INDEFINITE_BYTES BOOL ON "")
define_overridable_option(FLUF_WITH_CBOR_STRING_TIME BOOL ON "")
define_overridable_option(FLUF_WITH_LWM2M12 BOOL ON "")
define_overridable_option(FLUF_WITH_LWM2M_CBOR BOOL ON "")
define_overridable_option(FLUF_WITHOUT_BOOTSTRAP_DISCOVER_CTX BOOL OFF "")
define_overridable_option(FLUF_WITHOUT_DISCOVER_CTX BOOL OFF "")
define_overridable_option(FLUF_WITHOUT_REGISTER_CTX BOOL OFF "")
define_overridable_option(FLUF_WITH_PLAINTEXT BOOL ON "")
define_overridable_option(FLUF_WITH_SENML_CBOR BOOL ON "")
define_overridable_option(FLUF_WITH_OPAQUE BOOL ON "")

define_overridable_option(FLUF_MAX_ALLOWED_OPTIONS_NUMBER STRING 15 "")
define_overridable_option(FLUF_MAX_ALLOWED_LOCATION_PATHS_NUMBER STRING 1 "")
define_overridable_option(FLUF_ATTR_OPTION_MAX_SIZE STRING 100 "")

# anj options
define_overridable_option(DM_WITH_LOGS BOOL ON "")
define_overridable_option(WITH_DDM BOOL ON "")
define_overridable_option(ANJ_WITH_SDM_LOGS BOOL ON "")
define_overridable_option(ANJ_WITH_FOTA_OBJECT BOOL ON "")
define_overridable_option(ANJ_FOTA_PULL_METHOD_SUPPORTED BOOL ON "")
define_overridable_option(ANJ_FOTA_PUSH_METHOD_SUPPORTED BOOL ON "")
define_overridable_option(ANJ_FOTA_PROTOCOL_COAP_SUPPORTED BOOL ON "")
define_overridable_option(ANJ_FOTA_PROTOCOL_COAPS_SUPPORTED BOOL ON "")
define_overridable_option(ANJ_FOTA_PROTOCOL_HTTP_SUPPORTED BOOL ON "")
define_overridable_option(ANJ_FOTA_PROTOCOL_HTTPS_SUPPORTED BOOL ON "")
define_overridable_option(ANJ_FOTA_PROTOCOL_COAP_TCP_SUPPORTED BOOL ON "")
define_overridable_option(ANJ_FOTA_PROTOCOL_COAP_TLS_SUPPORTED BOOL ON "")
define_overridable_option(ANJ_WITH_DEFAULT_SERVER_OBJ BOOL ON "")
define_overridable_option(ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER STRING 2 "")
define_overridable_option(ANJ_WITH_DEFAULT_SECURITY_OBJ BOOL ON "")
define_overridable_option(ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER STRING 2 "")
define_overridable_option(ANJ_PUBLIC_KEY_OR_IDENTITY_MAX_SIZE STRING 255 "")
define_overridable_option(ANJ_SERVER_PUBLIC_KEY_MAX_SIZE STRING 255 "")
define_overridable_option(ANJ_SECRET_KEY_MAX_SIZE STRING 255 "")
define_overridable_option(ANJ_WITH_DEFAULT_DEVICE_OBJ BOOL ON "")
define_overridable_option(ANJ_WITH_TIME_POSIX_COMPAT BOOL ON "")

set(repo_root "${CMAKE_CURRENT_LIST_DIR}/..")
set(config_root "${CMAKE_BINARY_DIR}/anj_config")
set(config_base_paths fluf/fluf_config.h anj/anj_config.h)

foreach(config_base_path ${config_base_paths})
  configure_file("${repo_root}/include_public/${config_base_path}.in"
                 "${config_root}/${config_base_path}")
endforeach()

add_subdirectory("${repo_root}/deps/avs_commons"
                 "${CMAKE_BINARY_DIR}/avs_commons_build")

file(GLOB_RECURSE fluf_sources "${repo_root}/src/fluf/*.c")
file(GLOB_RECURSE anj_sources "${repo_root}/src/anj/*.c")

add_library(avs_commons INTERFACE)
target_link_libraries(avs_commons INTERFACE avs_algorithm avs_utils
                                            avs_commons_global_headers)
if(WITH_AVS_LOG)
  target_link_libraries(avs_commons INTERFACE avs_log)
endif()
if(ANJAY_TESTING)
  target_link_libraries(avs_commons INTERFACE avs_unit)
endif()

add_library(anjay_shared_headers INTERFACE)
target_include_directories(
  anjay_shared_headers INTERFACE "${repo_root}/include_public" "${config_root}")

add_library(fluf STATIC ${fluf_sources})
target_link_libraries(fluf anjay_shared_headers avs_commons)

add_library(anj STATIC ${anj_sources})
if(ANJAY_TESTING)
  target_include_directories(anj PRIVATE "${repo_root}/tests/anj")
endif()
target_link_libraries(anj fluf)

foreach(target IN LISTS avs_commons fluf anj)
  set_property(TARGET ${target} PROPERTY C_STANDARD 11)
  set_property(TARGET ${target} PROPERTY C_EXTENSIONS OFF)

  if(gcc_or_clang AND ANJAY_WITH_EXTRA_WARNINGS)
    target_compile_options(
      ${target}
      PUBLIC -pedantic
             -Wall
             -Wextra
             -Winit-self
             -Wmissing-declarations
             -Wc++-compat
             -Wsign-conversion
             -Wconversion
             -Wcast-qual
             -Wvla
             -Wno-variadic-macros
             -Wno-long-long
             -Wshadow)
  endif()
endforeach()

if(ANJAY_TESTING AND gcc_or_clang)
  foreach(target IN LISTS fluf anj)
    target_compile_definitions(${target} PRIVATE -DUNIT_TESTING -Wno-pedantic
                                                 -Wno-c++-compat)
  endforeach()
endif()
