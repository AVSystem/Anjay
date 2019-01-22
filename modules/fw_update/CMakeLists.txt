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

set(SOURCES
    src/fw_dm_security.c
    src/fw_update.c)
set(PUBLIC_HEADERS
    include_public/anjay/fw_update.h)

set(TEST_SOURCES
    ${SOURCES}
    ${PUBLIC_HEADERS})

include(../module_common.cmake)
