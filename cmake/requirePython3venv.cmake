# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

include_guard(GLOBAL)
set(Python_FIND_FRAMEWORK LAST)
set(Python3_FIND_VIRTUALENV ONLY)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import sys; print(sys.base_prefix != sys.prefix)"
    OUTPUT_VARIABLE _is_venv
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT _is_venv STREQUAL "True")
    message(FATAL_ERROR
    "Python3_EXECUTABLE=${Python3_EXECUTABLE} is not from a virtualenv.\n"
    "Activate your .venv or pass -DPython3_EXECUTABLE=/path/to/.venv/bin/python")
endif()

function(get_venv_check_command OUT_CMD)
    set(PYTHON_SCRIPT 
        "import sys; \
        in_venv = (sys.prefix != sys.base_prefix); \
        print(f'Venv check: {sys.prefix}') if in_venv else print('\\nFATAL: Run make command inside python venv!\\n'); \
        sys.exit(not in_venv)"
    )

    # python3 is called instead of Python3_EXECUTABLE to allow entering venv after calling cmake
    set(${OUT_CMD} python3 -c "${PYTHON_SCRIPT}" PARENT_SCOPE)
endfunction()
