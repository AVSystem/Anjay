# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

include_guard(GLOBAL)

function (is_venv_available OUT_IS_VENV)
    set(Python_FIND_FRAMEWORK LAST)
    set(Python3_FIND_VIRTUALENV ONLY)
    
    unset(_Python3_EXECUTABLE CACHE)
    find_package(Python3 3.8 COMPONENTS Interpreter)
    set(Python3_EXECUTABLE "${Python3_EXECUTABLE}" PARENT_SCOPE)

    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import sys; print(sys.base_prefix != sys.prefix)"
        OUTPUT_VARIABLE _is_venv
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(${OUT_IS_VENV} ${_is_venv} PARENT_SCOPE)
endfunction()
