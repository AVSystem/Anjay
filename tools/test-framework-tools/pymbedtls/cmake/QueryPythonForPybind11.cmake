# This file is derived from:
#  py-build-cmake, examples/pybind11-project/cmake/QueryPythonForPybind11.cmake
#  https://github.com/tttapa/py-build-cmake/blob/ea6fad016da1069a41222c73f345f06c9049b059/examples/pybind11-project/cmake/QueryPythonForPybind11.cmake
#
# Note: The upsteam project is MIT-licensed, original license follows:
# MIT License

# Copyright (c) 2022 Pieter P

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

option(USE_GLOBAL_PYBIND11 "Don't query Python to find pybind11" Off)
mark_as_advanced(USE_GLOBAL_PYBIND11)

# First tries to find Python 3, then tries to import the pybind11 module to
# query the CMake config location, and finally imports pybind11 using
# find_package(pybind11 ${ARGN} REQUIRED CONFIG CMAKE_FIND_ROOT_PATH_BOTH),
# where ${ARGN} are the arguments passed to this macro.
macro(find_pybind11_python_first)

    # https://github.com/pybind/pybind11/pull/5083
    set(PYBIND11_USE_CROSSCOMPILING On)

    # Find Python
    if (CMAKE_CROSSCOMPILING AND NOT (APPLE AND "$ENV{CIBUILDWHEEL}" STREQUAL "1"))
        find_package(Python3 REQUIRED COMPONENTS Development.Module)
    else()
        find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
    endif()

    # Query Python to see if it knows where the pybind11 root is
    if (NOT USE_GLOBAL_PYBIND11 AND Python3_EXECUTABLE)
        if (NOT pybind11_ROOT OR NOT EXISTS ${pybind11_ROOT})
            message(STATUS "Detecting pybind11 CMake location")
            execute_process(COMMAND ${Python3_EXECUTABLE}
                    -m pybind11 --cmakedir
                OUTPUT_VARIABLE PY_BUILD_PYBIND11_ROOT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE PY_BUILD_CMAKE_PYBIND11_RESULT)
            # If it was successful
            if (PY_BUILD_CMAKE_PYBIND11_RESULT EQUAL 0)
                message(STATUS "pybind11 CMake location: ${PY_BUILD_PYBIND11_ROOT}")
                set(pybind11_ROOT ${PY_BUILD_PYBIND11_ROOT}
                    CACHE PATH "Path to the pybind11 CMake configuration." FORCE)
            else()
                unset(pybind11_ROOT CACHE)
            endif()
        endif()
    endif()

    # pybind11 is header-only, so finding a native version is fine
    find_package(pybind11 ${ARGN} REQUIRED CONFIG CMAKE_FIND_ROOT_PATH_BOTH)

endmacro()
