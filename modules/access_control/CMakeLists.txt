set(SOURCES
    src/access_control.c
    src/handlers.c
    src/persistence.c)
set(PRIVATE_HEADERS
    src/access_control.h)
set(PUBLIC_HEADERS
    include_public/anjay/access_control.h)

set(TEST_SOURCES
    ${SOURCES}
    ${PRIVATE_HEADERS}
    ${PUBLIC_HEADERS})

include(../module_common.cmake)
