cmake_minimum_required(VERSION 3.4.0)

add_compile_options(-std=c99 -pedantic -Wall -Wextra -Winit-self
                    -Wmissing-declarations -Wc++-compat -Wsign-conversion
                    -Wconversion -Wcast-qual -Wvla -Wno-variadic-macros
                    -Wno-long-long -Wshadow -O0 -g3)
#target_compile_options(fluf PUBLIC -O0 -g3 -Wall)

#avs_commons
project(avs_commons C)
file(GLOB_RECURSE commons_src
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/src/**/*.c"
    )

add_library(avs_commons ${commons_src})

target_include_directories(avs_commons PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/linux_config"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/include_public"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/src"
    )
target_link_libraries(avs_commons m pthread anl)


#fluf
project(fluf C)

file(GLOB_RECURSE fluf_src
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fluf/*.c"
    )
add_library(fluf ${fluf_src})

target_compile_definitions(fluf PUBLIC
        -DFLUF_WITH_LWM2M11
        -DFLUF_WITH_LWM2M12
        -DFLUF_WITH_SENML_CBOR
        -DFLUF_WITH_LWM2M_CBOR
        -DFLUF_WITH_CBOR
        -DFLUF_WITH_CBOR_INDEFINITE_BYTES
        -DFLUF_WITH_CBOR_DECIMAL_FRACTIONS
        -DFLUF_WITH_CBOR_HALF_FLOAT
        -DFLUF_WITH_CBOR_STRING_TIME)

target_include_directories(fluf PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include_public"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/include_public"
    )
target_link_libraries(fluf PUBLIC avs_commons)

# anjay_basic example
project(anjay_basic_example C)

file(GLOB_RECURSE anjay_lite_src
    "${CMAKE_CURRENT_SOURCE_DIR}/src/anjay_lite/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/anj/sdm/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/anj/compat/posix/anj_time.c")

add_executable(anjay_basic_example
               examples/anjay_basic/anjay_net_impl.c
               examples/anjay_basic/main.c
               ${anjay_lite_src})

target_include_directories(anjay_basic_example PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include_public"
    )

target_link_libraries(anjay_basic_example PUBLIC fluf)

# anjay_basic_with_send example
project(anjay_send_example C)

add_executable(anjay_send_example
               examples/anjay_basic_with_send/anjay_net_impl.c
               examples/anjay_basic_with_send/main.c
               ${anjay_lite_src})

target_include_directories(anjay_send_example PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include_public"
    )

target_link_libraries(anjay_send_example PUBLIC fluf)

# anjay_basic_with_firmware_update example
project(anjay_firmware_update_example C)

add_executable(anjay_firmware_update_example
               examples/anjay_basic_with_firmware_update/anjay_net_impl.c
               examples/anjay_basic_with_firmware_update/firmware_update.c
               examples/anjay_basic_with_firmware_update/main.c
               ${anjay_lite_src})

target_include_directories(anjay_firmware_update_example PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/anjay_basic_with_firmware_update"
    "${CMAKE_CURRENT_SOURCE_DIR}/include_public"
    )

target_link_libraries(anjay_firmware_update_example PUBLIC fluf)


#fluf_tests
project(fluf_tests C)

file(GLOB fluf_tests_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fluf/*.c")

add_executable(fluf_tests
                ${fluf_tests_sources})

target_include_directories(fluf_tests PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include_public"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/include_public"
    )
target_link_libraries(fluf_tests PUBLIC fluf)

target_compile_options(fluf_tests PUBLIC -Wno-pedantic -Wno-c++-compat)

#dm_tests
project(dm_tests C)

file(GLOB dm_test_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/anj/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/anj/dm/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/anj/dm/*.c")

add_executable(dm_tests
                ${dm_test_sources})

target_compile_definitions(dm_tests PUBLIC
        -DUNIT_TESTING)

target_include_directories(dm_tests PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include_public"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/include_public"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/anj/dm")

target_link_libraries(dm_tests PUBLIC avs_commons)

target_compile_options(dm_tests PUBLIC -Wno-pedantic -Wno-c++-compat)

#sdm_tests
project(sdm_tests C)

file(GLOB sdm_test_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/anj/sdm/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/anj/sdm/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/anj/compat/posix/anj_time.c")

add_executable(sdm_tests
                ${sdm_test_sources})

target_compile_definitions(sdm_tests PUBLIC -DUNIT_TESTING)

target_include_directories(sdm_tests PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include_public"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/avs_commons/include_public")

target_link_libraries(sdm_tests PUBLIC fluf)

target_compile_options(sdm_tests PUBLIC -Wno-pedantic -Wno-c++-compat)

#valgrind targets
find_program(VALGRIND_EXECUTABLE valgrind)
if(VALGRIND_EXECUTABLE)
    add_custom_target(fluf_tests_with_valgrind
        COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --track-origins=yes -q --error-exitcode=63 $<TARGET_FILE:fluf_tests>)
    add_custom_target(dm_tests_with_valgrind
            COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --track-origins=yes -q --error-exitcode=63 $<TARGET_FILE:dm_tests>)
    add_custom_target(sdm_tests_with_valgrind
            COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --track-origins=yes -q --error-exitcode=63 $<TARGET_FILE:sdm_tests>)
endif()
