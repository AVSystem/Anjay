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
