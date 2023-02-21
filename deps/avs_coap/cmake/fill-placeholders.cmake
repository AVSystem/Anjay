# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem CoAP library
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

string(TIMESTAMP current_year "%Y")

foreach(file IN LISTS CMAKE_INSTALL_MANIFEST_FILES)
    if(file MATCHES ".(h|hpp|c|cpp|cmake|py|sh)$")
        file(READ ${file} file_contents)
        string(REPLACE "2017-2023" "${current_year}" file_contents_replaced "${file_contents}")
        file(WRITE ${file} "${file_contents_replaced}")
    endif()
endforeach()
