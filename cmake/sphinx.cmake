# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

function(sphinx_generate
         OUTBASE
         OUTTITLE
         INCLUDE_FILES
         INCLUDE_ROOT_PATH)
    message(STATUS "generating sphinx sources for ${OUTBASE}")

    file(MAKE_DIRECTORY "${OUTBASE}")
    set(SHORTNAMES)
    foreach(FNAME ${INCLUDE_FILES})
        string(REGEX REPLACE "^.*/([^.]*)[.]h$" "\\1" SHORTNAME "${FNAME}")
        list(APPEND SHORTNAMES "${SHORTNAME}")

        file(RELATIVE_PATH RELATIVE_FNAME ${INCLUDE_ROOT_PATH} ${FNAME})

        set(TITLE "``${RELATIVE_FNAME}``")
        string(LENGTH "${TITLE}" TITLELEN)
        string(RANDOM LENGTH ${TITLELEN} ALPHABET "#" TITLEHEAD)

        message(STATUS "generating ${OUTBASE}/${SHORTNAME}.rst")

        file(WRITE "${OUTBASE}/${SHORTNAME}.rst"
"${TITLEHEAD}
${TITLE}
${TITLEHEAD}

.. contents:: :local:

.. highlight:: c

.. doxygenfile:: ${RELATIVE_FNAME}")
    endforeach()

    string(LENGTH "${OUTTITLE}" TITLELEN)
    string(RANDOM LENGTH ${TITLELEN} ALPHABET "#" OUTTITLEHEADER)
    string(REGEX REPLACE "^.*/" "" SHORTOUTBASE "${OUTBASE}")

    set(OUTPUT
"${OUTTITLEHEADER}
${OUTTITLE}
${OUTTITLEHEADER}

.. toctree::
")

    foreach(SHORTNAME ${SHORTNAMES})
        set(OUTPUT "${OUTPUT}
    ${SHORTOUTBASE}/${SHORTNAME}")
    endforeach()
    file(WRITE "${OUTBASE}.rst" "${OUTPUT}")
endfunction()
