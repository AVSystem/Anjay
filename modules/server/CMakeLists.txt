set(SOURCES
    src/server.c
    src/persistence.c
    src/transaction.c
    src/utils.c)
set(PUBLIC_HEADERS
    include_public/anjay/server.h)

set(TEST_SOURCES
    ${SOURCES}
    ${PUBLIC_HEADERS})

include(../module_common.cmake)
