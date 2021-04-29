# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

add_custom_target(avs_coap_doc)
if(NOT TARGET doc)
    add_custom_target(doc)
endif()
add_dependencies(doc avs_coap_doc)

set(AVS_COAP_SPHINX_DOC_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sphinx/source")
set(AVS_COAP_SPHINX_DOC_CONF_DIR "${CMAKE_CURRENT_BINARY_DIR}/sphinx")

if(EXISTS "${AVS_COAP_SPHINX_DOC_ROOT_DIR}/conf.py.in")
    find_program(SPHINX_BUILD_EXECUTABLE sphinx-build
                 HINTS $ENV{SPHINX_DIR} PATH_SUFFIXES bin)

    configure_file(${AVS_COAP_SPHINX_DOC_ROOT_DIR}/conf.py.in
                   ${AVS_COAP_SPHINX_DOC_CONF_DIR}/conf.py
                   @ONLY)
    add_custom_target(avs_coap_doc_sphinx
                      COMMAND ${SPHINX_BUILD_EXECUTABLE}
                              -b html
                              -c ${AVS_COAP_SPHINX_DOC_CONF_DIR}
                              ${AVS_COAP_SPHINX_DOC_ROOT_DIR}
                              ${AVS_COAP_SPHINX_DOC_CONF_DIR}/html
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/sphinx)
    add_custom_command(TARGET avs_coap_doc_sphinx POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E remove -f api
                       COMMAND ${CMAKE_COMMAND} -E create_symlink ../../doxygen/html api
                       WORKING_DIRECTORY "${AVS_COAP_SPHINX_DOC_CONF_DIR}/html")

    add_dependencies(avs_coap_doc avs_coap_doc_sphinx)
endif()

if(EXISTS "${AVS_COAP_SOURCE_DIR}/Doxyfile.in")
    find_package(Doxygen)

    get_target_property(DOXYGEN_INPUT_PATHS avs_coap INTERFACE_INCLUDE_DIRECTORIES)
    # doxygen expects whitespace-separated list, cmake stores them as
    # semicolon-separated strings
    string(REPLACE ";" " " DOXYGEN_INPUT_PATHS "${DOXYGEN_INPUT_PATHS}")

    set(DOXYFILE_PREDEFINED_MACROS)
    # TODO: List these flags automatically
    foreach(FLAG WITH_AVS_COAP_BLOCK
                 WITH_AVS_COAP_OBSERVE
                 WITH_AVS_COAP_OBSERVE_PERSISTENCE
                 WITH_AVS_COAP_STREAMING_API
                 WITH_AVS_COAP_UDP)
        if("${${FLAG}}")
            set(DOXYFILE_PREDEFINED_MACROS "${DOXYFILE_PREDEFINED_MACROS} ${FLAG}")
        endif()
    endforeach()

    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)

    # configure_file does not expand CMake generator expressions; file(GENERATE) does
    configure_file(${AVS_COAP_SOURCE_DIR}/Doxyfile.in
                   ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile.with-cmake-generator-expressions
                   @ONLY)
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile
         INPUT ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile.with-cmake-generator-expressions)

    add_custom_target(avs_coap_doc_doxygen
                      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile
                      WORKING_DIRECTORY ${AVS_COAP_SOURCE_DIR}
                      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile)

    if(TARGET avs_coap_doc_sphinx)
        add_dependencies(avs_coap_doc_sphinx avs_coap_doc_doxygen)
    endif()
    add_dependencies(avs_coap_doc avs_coap_doc_doxygen)
endif()
