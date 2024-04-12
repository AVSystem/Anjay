cmake_minimum_required(VERSION 3.4.0)

# Core CMake code is located in cmake/anjay-config.cmake and the library is
# supposed to be imported using find_package() mechanism - this file only meant
# to be a convenience wrapper for all example and test targets.

project(anjay C)

# Below find_package() call is meant for IDE indexing purposes.
set(anjay_DIR "cmake")
find_package(anjay 4 REQUIRED)

find_program(VALGRIND_EXECUTABLE valgrind)

function(add_standalone_target NAME PATH WITH_VALGRIND)
  set(workdir "${CMAKE_BINARY_DIR}/${NAME}")

  string(REPLACE " " ";" unescaped_c_flags "${CMAKE_C_FLAGS}")

  add_custom_target(${NAME} ALL)
  add_custom_command(TARGET ${NAME} COMMAND ${CMAKE_COMMAND} -E make_directory
                                            "${workdir}")
  add_custom_command(
    TARGET ${NAME}
    COMMAND
      ${CMAKE_COMMAND} -S "${CMAKE_CURRENT_SOURCE_DIR}/${PATH}" -B .
      -DCMAKE_C_COMPILER="${CMAKE_C_COMPILER}"
      -DCMAKE_C_FLAGS="${unescaped_c_flags}"
    COMMAND ${CMAKE_COMMAND} --build . --target ${NAME} -- -j${NPROC}
    WORKING_DIRECTORY "${workdir}")
  if(WITH_VALGRIND AND VALGRIND_EXECUTABLE)
    add_custom_target(
      ${NAME}_with_valgrind
      DEPENDS ${NAME}
      COMMAND "${VALGRIND_EXECUTABLE}" --leak-check=full --track-origins=yes -q
              --error-exitcode=63 "${workdir}/${NAME}")
  endif()
endfunction()

add_standalone_target(dm_tests tests/anj/dm ON)
add_standalone_target(sdm_tests tests/anj/sdm ON)
add_standalone_target(fluf_tests tests/fluf ON)
add_standalone_target(minimal_client_example examples/minimal_client OFF)
add_standalone_target(send_example examples/send OFF)
add_standalone_target(firmware_update_example examples/firmware_update OFF)
add_standalone_target(bootstrap_example examples/bootstrap OFF)
