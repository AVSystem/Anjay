# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

set(AFL_FUZZER_DIR "" CACHE STRING "AFL fuzzer binary directory")
if(AFL_FUZZER_DIR)
    set(CMAKE_C_COMPILER "${AFL_FUZZER_DIR}/afl-gcc")
else()
    set(CMAKE_C_COMPILER afl-gcc)
endif()
