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

make_absolute_sources(ABSOLUTE_MODULE_SOURCES
                      ${SOURCES}
                      ${PRIVATE_HEADERS}
                      ${MODULES_HEADERS}
                      ${PUBLIC_HEADERS})

make_absolute_sources(ABSOLUTE_MODULE_TEST_SOURCES
                      ${TEST_SOURCES})

list(APPEND ABSOLUTE_SOURCES ${ABSOLUTE_MODULE_SOURCES})
set(ABSOLUTE_SOURCES "${ABSOLUTE_SOURCES}" PARENT_SCOPE)

list(APPEND ABSOLUTE_TEST_SOURCES ${ABSOLUTE_MODULE_TEST_SOURCES})
set(ABSOLUTE_TEST_SOURCES "${ABSOLUTE_TEST_SOURCES}" PARENT_SCOPE)
