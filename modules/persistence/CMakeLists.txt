set(SOURCES
    src/persistence.c)
set(PUBLIC_HEADERS
    include_public/anjay/persistence.h)

set(TEST_SOURCES
    ${SOURCES}
    ${PUBLIC_HEADERS})

include(../module_common.cmake)

