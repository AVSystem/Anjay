# Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

add_custom_target(anjay_doc)
if(NOT TARGET doc)
    add_custom_target(doc)
endif()
add_dependencies(doc anjay_doc)

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
    add_custom_target(anjay_doc_sphinx
                      COMMAND ${SPHINX_BUILD_EXECUTABLE}
                              -b html
                              -c ${ANJAY_SPHINX_DOC_CONF_DIR}
                              ${ANJAY_SPHINX_DOC_ROOT_DIR}
                              ${ANJAY_SPHINX_DOC_CONF_DIR}/html
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/sphinx)
    add_custom_command(TARGET anjay_doc_sphinx POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E remove -f api
                       COMMAND ${CMAKE_COMMAND} -E create_symlink ../../doxygen/html api
                       WORKING_DIRECTORY "${ANJAY_BUILD_OUTPUT_DIR}/doc/sphinx/html")

    add_dependencies(anjay_doc anjay_doc_sphinx)

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
                     "SNIPPET_SOURCE_MD5FILE=${CMAKE_CURRENT_SOURCE_DIR}/sphinx/snippet_sources.md5")

        add_custom_target(anjay_doc_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_doc_.*$$'")
        add_dependencies(anjay_unit_check anjay_doc_check)
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

    add_custom_target(anjay_doc_doxygen
                      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile
                      WORKING_DIRECTORY ${ANJAY_SOURCE_DIR}
                      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile)

    if(TARGET anjay_doc_sphinx)
        add_dependencies(anjay_doc_sphinx anjay_doc_doxygen)
    endif()
    add_dependencies(anjay_doc anjay_doc_doxygen)
endif()
