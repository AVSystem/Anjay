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

add_custom_target(doc)

set(ANJAY_SPHINX_DOC_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sphinx/source")
set(ANJAY_SPHINX_DOC_CONF_DIR "${ANJAY_BUILD_OUTPUT_DIR}/doc/sphinx")

cmake_dependent_option(WITH_DOC_CHECK "Enables/disables sphinx documentation validation" OFF WITH_TEST OFF)
cmake_dependent_option(WITH_URL_CHECK "Enables/disables URL validation" OFF "WITH_TEST;WITH_DOC_CHECK" OFF)

if(EXISTS "${ANJAY_SPHINX_DOC_ROOT_DIR}/conf.py.in")
    find_program(SPHINX_BUILD_EXECUTABLE sphinx-build
                 HINTS $ENV{SPHINX_DIR} PATH_SUFFIXES bin)

    include(${ANJAY_SOURCE_DIR}/cmake/sphinx.cmake)

    configure_file(${ANJAY_SPHINX_DOC_ROOT_DIR}/conf.py.in
                   ${ANJAY_SPHINX_DOC_CONF_DIR}/conf.py
                   @ONLY)
    add_custom_target(doc_sphinx
                      COMMAND ${SPHINX_BUILD_EXECUTABLE}
                              -b html
                              -c ${ANJAY_SPHINX_DOC_CONF_DIR}
                              ${ANJAY_SPHINX_DOC_ROOT_DIR}
                              ${ANJAY_SPHINX_DOC_CONF_DIR}/html
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/sphinx)
    add_custom_command(TARGET doc_sphinx POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E remove -f api
                       COMMAND ${CMAKE_COMMAND} -E create_symlink ../../doxygen/html api
                       WORKING_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/doc/sphinx/html")

    add_dependencies(doc doc_sphinx)

    if(WITH_DOC_CHECK)
        if(NOT SPHINX_BUILD_EXECUTABLE)
            message(FATAL_ERROR "Sphinx not found (this can be suppressed by WITH_DOC_CHECK=OFF)")
        endif()
        # sphinx-based .. snippet-source:: validation
        add_test(NAME test_doc_snippet
                 COMMAND ${SPHINX_BUILD_EXECUTABLE}
                         -Q -b snippet_source_lint
                         -c ${ANJAY_SPHINX_DOC_CONF_DIR}
                         ${ANJAY_SPHINX_DOC_ROOT_DIR}
                         ${ANJAY_SPHINX_DOC_CONF_DIR}/lint
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/sphinx)
        if(WITH_URL_CHECK)
            add_test(NAME test_doc_url
                     COMMAND ./runtest.py
                     WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/tests/doc)
        endif()
        set_property(TEST test_doc_snippet APPEND PROPERTY ENVIRONMENT
                     "ANJAY_SPHINX_DOC_CONF_DIR=${ANJAY_SPHINX_DOC_CONF_DIR}"
                     "ANJAY_SPHINX_DOC_ROOT_DIR=${ANJAY_SPHINX_DOC_ROOT_DIR}"
                     "CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                     "SNIPPET_SOURCE_MD5FILE=${ANJAY_SOURCE_DIR}/examples/test_examples.md5")

        add_custom_target(doc_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_doc_.*$$'")
        add_dependencies(anjay_unit_check doc_check)
    endif()
endif()

if(EXISTS "${ANJAY_SOURCE_DIR}/Doxyfile.in")
    find_package(Doxygen)

    get_target_property(DOXYGEN_INPUT_PATHS anjay INTERFACE_INCLUDE_DIRECTORIES)
    # doxygen expects whitespace-separated list, cmake stores them as
    # semicolon-separated strings
    string(REPLACE ";" " " DOXYGEN_INPUT_PATHS "${DOXYGEN_INPUT_PATHS}")

    execute_process(COMMAND ${ANJAY_SOURCE_DIR}/tools/anjay_config_log_tool.py list_flags
                    RESULT_VARIABLE LIST_FLAGS_RESULT
                    OUTPUT_VARIABLE DOXYFILE_PREDEFINED_MACROS)
    if(NOT LIST_FLAGS_RESULT EQUAL 0)
        message(WARNING "Could not enumerate configuration macros")
    endif()
    string(REPLACE "\n" " " DOXYFILE_PREDEFINED_MACROS "${DOXYFILE_PREDEFINED_MACROS}")

    set(DOXYGEN_OUTPUT_DIR ${ANJAY_BUILD_OUTPUT_DIR}/doc/doxygen)

    # configure_file does not expand CMake generator expressions; file(GENERATE) does
    configure_file(${ANJAY_SOURCE_DIR}/Doxyfile.in
                   ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile.with-cmake-generator-expressions
                   @ONLY)
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile
         INPUT ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile.with-cmake-generator-expressions)

    add_custom_target(doc_doxygen
                      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile
                      WORKING_DIRECTORY ${ANJAY_SOURCE_DIR}
                      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile)

    if(TARGET doc_sphinx)
        add_dependencies(doc_sphinx doc_doxygen)
    endif()
    add_dependencies(doc doc_doxygen)
endif()
