# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

include(../cmake/requirePython3venv.cmake)
get_venv_check_command(CHECK_VENV)

set(ANJAY_DOC_SOURCE_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/sphinx/source")
set(ANJAY_DOC_SOURCE_API "${CMAKE_CURRENT_SOURCE_DIR}/sphinx/source_api")

set(ANJAY_DOC_CONF_DIR_BASE "${ANJAY_BUILD_OUTPUT_DIR}/doc/sphinx")
set(ANJAY_DOC_OUTPUT_DIR "${ANJAY_DOC_CONF_DIR_BASE}/html")

set(DOXYGEN_OUTPUT_DIR "${ANJAY_BUILD_OUTPUT_DIR}/doc/doxygen")
set(DOXYGEN_INPUT_PATHS "${CMAKE_BINARY_DIR}/output/doc/doxygen/config")
set(DOXYGEN_PREDEFINED_FILE "${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile.defines")

cmake_dependent_option(WITH_DOC_CHECK "Enables/disables sphinx documentation validation" OFF WITH_TEST OFF)
cmake_dependent_option(WITH_URL_CHECK "Enables/disables URL validation" OFF "WITH_TEST;WITH_DOC_CHECK" OFF)
cmake_dependent_option(WITH_STRICT_URL_CHECK "Enables/disables strict URL validation" OFF "WITH_TEST;WITH_DOC_CHECK;WITH_URL_CHECK" OFF)

find_program(SPHINX_BUILD_EXECUTABLE sphinx-build
                 HINTS $ENV{SPHINX_DIR} PATH_SUFFIXES bin)
find_package(Doxygen)

# make sure both programs are available and templates for configs exist
if(SPHINX_BUILD_EXECUTABLE AND DOXYGEN_FOUND AND
   EXISTS "${ANJAY_DOC_SOURCE_MAIN}/conf.py.in" AND
   EXISTS "${ANJAY_DOC_SOURCE_API}/conf.py.in" AND
   EXISTS "${ANJAY_SOURCE_DIR}/Doxyfile.in")

    # generate all-enabled config and extract the options to DOXYGEN_PREDEFINED_FILE
    # see tools/generate_doxygen_config.py script description
    execute_process(
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/generate_doxygen_config.py
                "${DOXYGEN_PREDEFINED_FILE}"
        RESULT_VARIABLE _gen_result
    )

    if(NOT _gen_result EQUAL 0)
        message(FATAL_ERROR "Failed to generate config and predefined macros")
    endif()

    # read from DOXYGEN_PREDEFINED_FILE file and list all the options to DOXYFILE_PREDEFINED_MACROS
    file(READ "${DOXYGEN_PREDEFINED_FILE}" DOXYFILE_PREDEFINED_MACROS)
    string(STRIP "${DOXYFILE_PREDEFINED_MACROS}" DOXYFILE_PREDEFINED_MACROS)

    # generatge Doxyfile basing on set variables and Doxyfile.in template
    configure_file(${ANJAY_SOURCE_DIR}/Doxyfile.in
                   ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile.with-cmake-generator-expressions
                   @ONLY)
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile
         INPUT ${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile.with-cmake-generator-expressions)

    # configure Sphinx config file for main documentation
    set(CURRENT_TEMPLATES_DIR "${ANJAY_DOC_SOURCE_MAIN}/_templates")
    configure_file(${ANJAY_DOC_SOURCE_MAIN}/conf.py.in
                   ${ANJAY_DOC_CONF_DIR_BASE}/conf_main/conf.py
                   @ONLY)

    # configure Sphinx config file for API docs
    set(CURRENT_TEMPLATES_DIR "${ANJAY_DOC_SOURCE_API}/_templates")
    configure_file(${ANJAY_DOC_SOURCE_API}/conf.py.in
                   ${ANJAY_DOC_CONF_DIR_BASE}/conf_api/conf.py
                   @ONLY)

    # declare the common target for docs
    add_custom_target(anjay_doc
        # ensure venv is active with funciton and command imported from requirePython3venv.cmake
        COMMAND ${CHECK_VENV}

        # ensure headers in output folder are up-to-date and generate Doxygen XML files
        # (smart/conditional copy and build inside this script)
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/build_doxygen_docs.py
                "${DOXYGEN_INPUT_PATHS}"
                "${CMAKE_CURRENT_BINARY_DIR}/doxygen/Doxyfile"
                "${DOXYGEN_OUTPUT_DIR}/doxygen.md5"

        # generate Sphinx API docs
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/build_sphinx_docs.py
                "${ANJAY_DOC_SOURCE_API}"
                "${ANJAY_DOC_CONF_DIR_BASE}/conf_api"
                "${ANJAY_DOC_OUTPUT_DIR}/api"
                "${ANJAY_DOC_OUTPUT_DIR}/api/sphinx.md5"
                "${DOXYGEN_OUTPUT_DIR}/xml"

        # generate Sphinx main documentation
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/build_sphinx_docs.py
                "${ANJAY_DOC_SOURCE_MAIN}"
                "${ANJAY_DOC_CONF_DIR_BASE}/conf_main"
                "${ANJAY_DOC_OUTPUT_DIR}"
                "${ANJAY_DOC_OUTPUT_DIR}/sphinx.md5"

        WORKING_DIRECTORY ${ANJAY_SOURCE_DIR}
        COMMENT "Building Complete Documentation (Doxygen -> Sphinx API -> Sphinx Main)"
        VERBATIM
    )

    # add target and append it to doc (if exists)
    if(NOT TARGET doc)
        add_custom_target(doc)
    endif()
    add_dependencies(doc anjay_doc)

    # optional documentation tests
    if(WITH_DOC_CHECK)
        # sphinx-based .. snippet-source:: validation
        add_test(NAME test_doc_snippet
                 COMMAND ${SPHINX_BUILD_EXECUTABLE}
                         -Q -b snippet_source_lint
                         -c ${ANJAY_DOC_CONF_DIR_BASE}/conf_main
                         ${ANJAY_DOC_SOURCE_MAIN}
                         ${ANJAY_DOC_CONF_DIR_BASE}/lint
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/sphinx)
        if(WITH_URL_CHECK)
            if(WITH_STRICT_URL_CHECK)
                add_test(NAME test_doc_url
                        COMMAND ./runtest.py --strict
                        WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/tests/doc)
            else()
                add_test(NAME test_doc_url
                        COMMAND ./runtest.py
                        WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/tests/doc)
            endif()
        endif()
        set_property(TEST test_doc_snippet APPEND PROPERTY ENVIRONMENT
                     "ANJAY_DOC_CONF_DIR_BASE=${ANJAY_DOC_CONF_DIR_BASE}"
                     "ANJAY_SPHINX_DOC_ROOT_DIR=${ANJAY_DOC_SOURCE_MAIN}"
                     "CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                     "SNIPPET_SOURCE_MD5FILE=${CMAKE_CURRENT_SOURCE_DIR}/sphinx/snippet_sources.md5")

        add_custom_target(anjay_doc_check COMMAND ${CMAKE_CTEST_COMMAND} -V -R "'^test_doc_.*$$'")
        add_dependencies(anjay_unit_check anjay_doc_check)
    endif()
else()
    message(STATUS "Documentation build disabled: Sphinx or Doxygen not found, or configuration files missing.")
endif()
