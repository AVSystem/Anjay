# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

include_guard(GLOBAL)

include(${CMAKE_CURRENT_LIST_DIR}/checkPython3venv.cmake)

set(_is_venv "")
is_venv_available(_is_venv)

if(NOT _is_venv STREQUAL "True")
    message(FATAL_ERROR
    "Python3_EXECUTABLE is not from a virtualenv.\n"
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
