set(SOURCES
    src/security.c
    src/transaction.c
    src/persistence.c
    src/utils.c)
set(PUBLIC_HEADERS
    include_public/anjay/security.h)

set(TEST_SOURCES
    ${SOURCES}
    ${PUBLIC_HEADERS})

include(../module_common.cmake)
