# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

macro(anjay_emit_deps TNAME)
    list(APPEND ANJAY_TARGET_DEPS_${TNAME} ${ARGN})
endmacro()

macro(anjay_temp_name fname)
    if(${ARGC} GREATER 1) # Have to escape ARGC to correctly compare
        set(_base ${ARGV1})
    else(${ARGC} GREATER 1)
        set(_base ".cmake-tmp")
    endif(${ARGC} GREATER 1)
    set(_counter 0)
    while(EXISTS "${CMAKE_BINARY_DIR}/${_base}${_counter}")
        math(EXPR _counter "${_counter} + 1")
    endwhile(EXISTS "${CMAKE_BINARY_DIR}/${_base}${_counter}")
    set(${fname} "${CMAKE_BINARY_DIR}/${_base}${_counter}")
endmacro()

macro(anjay_find_library_inner)
    anjay_temp_name(_fname)
    file(WRITE ${_fname} "${anjay_find_library_EXPR_NOW}")
    include(${_fname})
    file(REMOVE ${_fname})
    foreach(PROVIDED_LIB ${ARGN})
        set(LIBRARY_FIND_ROUTINE_${PROVIDED_LIB} "${anjay_find_library_EXPR_STORE}")
        set(LIBRARY_FIND_ROUTINE_PROVIDES_${PROVIDED_LIB} ${ARGN})
    endforeach()
endmacro()

macro(anjay_find_library_ex EXPR_NOW EXPR_STORE)
    set(anjay_find_library_EXPR_NOW "${EXPR_NOW}")
    set(anjay_find_library_EXPR_STORE "${EXPR_STORE}")
    anjay_find_library_inner(${ARGN})
    set(anjay_find_library_EXPR_NOW)
    set(anjay_find_library_EXPR_STORE)
endmacro()

macro(anjay_find_library EXPR)
    set(anjay_find_library_EXPR_NOW "${EXPR}")
    set(anjay_find_library_EXPR_STORE "${EXPR}")
    anjay_find_library_inner(${ARGN})
    set(anjay_find_library_EXPR_NOW)
    set(anjay_find_library_EXPR_STORE)
endmacro()

set(LIBRARY_FIND_ROUTINES)
macro(anjay_emit_find_dependency LIB)
    if(NOT LIBRARY_FIND_ROUTINE_EMITTED_${LIB})
        if(LIBRARY_FIND_ROUTINE_${LIB})
            set(LIBRARY_FIND_ROUTINES "${LIBRARY_FIND_ROUTINES}

${LIBRARY_FIND_ROUTINE_${LIB}}")
            foreach(PROVIDED_LIB ${LIBRARY_FIND_ROUTINE_PROVIDES_${LIB}})
                set(LIBRARY_FIND_ROUTINE_EMITTED_${PROVIDED_LIB} 1)
            endforeach()
        endif()
    endif()
endmacro()

macro(anjay_install_export TNAME)
    install(TARGETS ${TNAME} EXPORT ${CMAKE_PROJECT_NAME}-targets DESTINATION ${LIB_INSTALL_DIR})
    foreach(DEP ${ANJAY_TARGET_DEPS_${TNAME}})
        anjay_emit_find_dependency(${DEP})
    endforeach()
endmacro()
