set(SOURCES
    src/attr_storage.c
    src/persistence.c)
set(PRIVATE_HEADERS
    src/attr_storage.h)
set(PUBLIC_HEADERS
    include_public/anjay/attr_storage.h)

set(TEST_SOURCES
    ${SOURCES}
    ${PRIVATE_HEADERS}
    ${PUBLIC_HEADERS}
    src/test/attr_storage_test.h)

include(../module_common.cmake)
