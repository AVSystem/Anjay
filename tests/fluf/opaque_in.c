/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#define TEST_ENV(Data, Path, PayloadFinished)                                \
    fluf_io_in_ctx_t ctx;                                                    \
    ASSERT_OK(fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,     \
                                  &(Path), FLUF_COAP_FORMAT_OPAQUE_STREAM)); \
    const fluf_res_value_t *value = NULL;                                    \
    const fluf_uri_path_t *path = NULL;                                      \
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, Data, sizeof(Data) - 1,      \
                                          PayloadFinished))

static const fluf_uri_path_t TEST_INSTANCE_PATH =
        _FLUF_URI_PATH_INITIALIZER(3, 4, FLUF_ID_INVALID, FLUF_ID_INVALID, 2);

#define MAKE_TEST_RESOURCE_PATH(Rid)                              \
    (FLUF_MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[FLUF_ID_OID], \
                             TEST_INSTANCE_PATH.ids[FLUF_ID_IID], (Rid)))

AVS_UNIT_TEST(opaque_in, disambiguation) {
    char TEST_DATA[] = "Hello, world!";
    TEST_ENV(TEST_DATA, MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_ANY;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_NOT_NULL(value);
    ASSERT_NOT_NULL(path);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(TEST_DATA) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "Hello, world!");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(opaque_in, bytes) {
    char TEST_DATA[] = "Hello, world!";
    TEST_ENV(TEST_DATA, MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_NOT_NULL(value);
    ASSERT_NOT_NULL(path);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(TEST_DATA) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "Hello, world!");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(opaque_in, bytes_in_parts) {
    char TEST_DATA_1[] = "Hello";
    char TEST_DATA_2[] = ", world!";
    TEST_ENV(TEST_DATA_1, MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(TEST_DATA_1) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "Hello");
    ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, TEST_DATA_2,
                                          sizeof(TEST_DATA_2) - 1, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(TEST_DATA_2) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, ", world!");
    ASSERT_EQ(value->bytes_or_string.full_length_hint,
              sizeof(TEST_DATA_1) + sizeof(TEST_DATA_2) - 2);
}

AVS_UNIT_TEST(opaque_in, unsupported_data_types) {
    const fluf_data_type_t DATA_TYPES[] = {
        FLUF_DATA_TYPE_NULL,   FLUF_DATA_TYPE_STRING, FLUF_DATA_TYPE_INT,
        FLUF_DATA_TYPE_DOUBLE, FLUF_DATA_TYPE_BOOL,   FLUF_DATA_TYPE_OBJLNK,
        FLUF_DATA_TYPE_UINT,   FLUF_DATA_TYPE_TIME
    };
    for (size_t i = 0; i < AVS_ARRAY_SIZE(DATA_TYPES); ++i) {
        char TEST_DATA[] = "Hello, world!";
        TEST_ENV(TEST_DATA, MAKE_TEST_RESOURCE_PATH(5), true);
        fluf_data_type_t type_bitmask = DATA_TYPES[i];
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
                  FLUF_IO_ERR_FORMAT);
        ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_NULL);
        ASSERT_NULL(value);
        ASSERT_NOT_NULL(path);
        ASSERT_TRUE(
                fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    }
}

AVS_UNIT_TEST(opaque_in, bytes_no_data_with_payload_finished) {
    TEST_ENV("", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 0);
    ASSERT_EQ(value->bytes_or_string.offset, 0);
    ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
    ASSERT_NULL(value->bytes_or_string.data);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_LOGIC);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_LOGIC);
}
