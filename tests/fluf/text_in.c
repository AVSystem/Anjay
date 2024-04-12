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

#define TEST_ENV(Data, Path, PayloadFinished)                            \
    fluf_io_in_ctx_t ctx;                                                \
    ASSERT_OK(fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, \
                                  &(Path), FLUF_COAP_FORMAT_PLAINTEXT)); \
    const fluf_res_value_t *value = NULL;                                \
    const fluf_uri_path_t *path = NULL;                                  \
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, Data, sizeof(Data) - 1,  \
                                          PayloadFinished))

static const fluf_uri_path_t TEST_INSTANCE_PATH =
        _FLUF_URI_PATH_INITIALIZER(3, 4, FLUF_ID_INVALID, FLUF_ID_INVALID, 2);

#define MAKE_TEST_RESOURCE_PATH(Rid)                              \
    (FLUF_MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[FLUF_ID_OID], \
                             TEST_INSTANCE_PATH.ids[FLUF_ID_IID], (Rid)))

AVS_UNIT_TEST(text_in, string) {
    char TEST_STRING[] = "Hello, world!";
    TEST_ENV(TEST_STRING, MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_STRING;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_STRING);
    ASSERT_NOT_NULL(value);
    ASSERT_NOT_NULL(path);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(TEST_STRING) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "Hello, world!");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(text_in, string_in_parts) {
    char TEST_STRING_1[] = "Hello";
    char TEST_STRING_2[] = ", world!";
    TEST_ENV(TEST_STRING_1, MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_STRING;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_STRING);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(TEST_STRING_1) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "Hello");
    ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, TEST_STRING_2,
                                          sizeof(TEST_STRING_2) - 1, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_STRING);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(TEST_STRING_2) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, ", world!");
    ASSERT_EQ(value->bytes_or_string.full_length_hint,
              sizeof(TEST_STRING_1) + sizeof(TEST_STRING_2) - 2);
}

#define TEST_NUM_COMMON(Val, ...)                         \
    do {                                                  \
        TEST_ENV(#Val, MAKE_TEST_RESOURCE_PATH(5), true); \
                                                          \
        __VA_ARGS__;                                      \
                                                          \
    } while (false)

#define TEST_NUM_FAIL(TypeBitmask, Val)                                     \
    TEST_NUM_COMMON(Val, fluf_data_type_t type_bitmask = TypeBitmask;       \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, \
                                                       &value, &path),      \
                              FLUF_IO_ERR_FORMAT);)

#define TEST_I64(Val)                                                        \
    TEST_NUM_COMMON(Val, fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT; \
                    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,  \
                                                       &value, &path));      \
                    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_INT);             \
                    ASSERT_TRUE(fluf_uri_path_equal(                         \
                            path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));       \
                    ASSERT_EQ(value->int_value, Val);                        \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,  \
                                                       &value, &path),       \
                              FLUF_IO_EOF);                                  \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,  \
                                                       &value, &path),       \
                              FLUF_IO_ERR_LOGIC);)

#define TEST_I64_FAIL(Val) TEST_NUM_FAIL(FLUF_DATA_TYPE_INT, Val)

AVS_UNIT_TEST(text_in, i64) {
    TEST_I64(514);
    TEST_I64(0);
    TEST_I64(-1);
    TEST_I64(2147483647);
    TEST_I64(-2147483648);
    TEST_I64(2147483648);
    TEST_I64(-2147483649);
    TEST_I64(9223372036854775807);
    // TODO obecna implementacja atoi po prostu dokonuje przepełnienia i nie
    // informuje o tym zostaje tak czy do dorobienia ?
    //    TEST_I64_FAIL(9223372036854775808);
    //    TEST_I64_FAIL(-9223372036854775809);
    TEST_I64_FAIL(1.0);
    TEST_I64_FAIL(wat);
}

// this is created as a separate test to avoid warning about too large integer
// constant
AVS_UNIT_TEST(text_in, smallest_i64) {
    TEST_ENV("-9223372036854775808", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_INT);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->int_value, INT64_MIN);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(text_in, int_started_not_finished) {
    TEST_ENV("514", MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
}

AVS_UNIT_TEST(text_in, int_not_started_not_finished) {
    TEST_ENV("", MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
}

AVS_UNIT_TEST(text_in, i64_in_parts) {
    char data_1[] = "-214";
    char data_2[] = "7483649";
    TEST_ENV(data_1, MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_INT);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_NULL(value);

    // second feed
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_2, strlen(data_2), true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_INT);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->int_value, -2147483649);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

#define TEST_U64(Val)                                                         \
    TEST_NUM_COMMON(Val, fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_UINT; \
                    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,   \
                                                       &value, &path));       \
                    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_UINT);             \
                    ASSERT_TRUE(fluf_uri_path_equal(                          \
                            path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));        \
                    ASSERT_EQ(value->uint_value, Val);                        \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,   \
                                                       &value, &path),        \
                              FLUF_IO_EOF);                                   \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,   \
                                                       &value, &path),        \
                              FLUF_IO_ERR_LOGIC);)

AVS_UNIT_TEST(text_in, u64) {
    TEST_U64(514);
    TEST_U64(514);
    TEST_U64(0);
    TEST_U64(2147483647);
    TEST_U64(2147483648);
    TEST_U64(4294967295);
    TEST_U64(4294967296);
}

// this is created as a separate test to avoid warning about too large unsigned
// integer constant
AVS_UNIT_TEST(text_in, biggest_u64) {
    TEST_ENV("18446744073709551615", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_UINT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_UINT);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->int_value, UINT64_MAX);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(text_in, u64_in_parts) {
    char data_1[] = "429";
    char data_2[] = "4967295";
    TEST_ENV(data_1, MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_UINT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_UINT);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_NULL(value);

    // second feed
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_2, strlen(data_2), true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_UINT);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->uint_value, 4294967295);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

#define TEST_DOUBLE(Val)                                                    \
    TEST_NUM_COMMON(Val,                                                    \
                    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_DOUBLE;  \
                    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, \
                                                       &value, &path));     \
                    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_DOUBLE);         \
                    ASSERT_TRUE(fluf_uri_path_equal(                        \
                            path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));      \
                    ASSERT_EQ(value->double_value, Val);                    \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, \
                                                       &value, &path),      \
                              FLUF_IO_EOF);                                 \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, \
                                                       &value, &path),      \
                              FLUF_IO_ERR_LOGIC);)

#define TEST_DOUBLE_FAIL(Val) TEST_NUM_FAIL(FLUF_DATA_TYPE_DOUBLE, Val)

AVS_UNIT_TEST(text_in, f64) {
    TEST_DOUBLE(0);
    TEST_DOUBLE(0.0);
    TEST_DOUBLE(1);
    TEST_DOUBLE(1.0);
    TEST_DOUBLE(1.2);
    TEST_DOUBLE(1.3125);
    TEST_DOUBLE(1.3125000);
    TEST_DOUBLE(-10000.5);
    TEST_DOUBLE(-10000000000000.5);
    // TODO? fluf_string_to_simple_double_value does not support exponential
    // notation
    //    TEST_DOUBLE(4.223e+37);
    //    TEST_DOUBLE(3.26e+218);
    TEST_DOUBLE_FAIL(wat);
}

AVS_UNIT_TEST(text_in, double_in_parts) {
    char data_1[] = "1.312";
    char data_2[] = "5000";
    TEST_ENV(data_1, MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_DOUBLE;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_DOUBLE);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_NULL(value);

    // second feed
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_2, strlen(data_2), true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_DOUBLE);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->double_value, 1.3125);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

#define TEST_BOOL(Val)                                                        \
    TEST_NUM_COMMON(Val, fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BOOL; \
                    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,   \
                                                       &value, &path));       \
                    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BOOL);             \
                    ASSERT_TRUE(fluf_uri_path_equal(                          \
                            path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));        \
                    AVS_UNIT_ASSERT_EQUAL(value->bool_value, Val);            \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,   \
                                                       &value, &path),        \
                              FLUF_IO_EOF);                                   \
                    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask,   \
                                                       &value, &path),        \
                              FLUF_IO_ERR_LOGIC);)

#define TEST_BOOL_FAIL(Str)                                             \
    do {                                                                \
        TEST_ENV(Str, MAKE_TEST_RESOURCE_PATH(5), true);                \
        fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BOOL;            \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, \
                                           &path),                      \
                  FLUF_IO_ERR_FORMAT);                                  \
        ASSERT_NULL(value);                                             \
        ASSERT_NULL(path);                                              \
    } while (false)

AVS_UNIT_TEST(text_in, boolean) {
    TEST_BOOL(0);
    TEST_BOOL(1);
    TEST_BOOL_FAIL("2");
    TEST_BOOL_FAIL("-1");
    TEST_BOOL_FAIL("true");
    TEST_BOOL_FAIL("false");
    TEST_BOOL_FAIL("wat");
}

AVS_UNIT_TEST(text_in, boolean_not_finished_afterwards) {
    TEST_ENV("1", MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BOOL;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_NULL(value);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));

    // call it second time to check proper behavior
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_NULL(value);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));

    // feed with nothing and with unfinished payload
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, "", 0, false));
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_NULL(value);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));

    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, "", 0, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->bool_value, true);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

#define TEST_OBJLNK_COMMON(Str, ...)                     \
    do {                                                 \
        TEST_ENV(Str, MAKE_TEST_RESOURCE_PATH(5), true); \
        __VA_ARGS__;                                     \
    } while (false)

#define TEST_OBJLNK(Oid, Iid)                                                  \
    TEST_OBJLNK_COMMON(#Oid ":" #Iid,                                          \
                       fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_OBJLNK;  \
                       ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, \
                                                          &value, &path));     \
                       ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_OBJLNK);         \
                       ASSERT_TRUE(fluf_uri_path_equal(                        \
                               path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));      \
                       AVS_UNIT_ASSERT_EQUAL(value->objlnk.oid, Oid);          \
                       AVS_UNIT_ASSERT_EQUAL(value->objlnk.iid, Iid);          \
                       ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, \
                                                          &value, &path),      \
                                 FLUF_IO_EOF);                                 \
                       ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, \
                                                          &value, &path),      \
                                 FLUF_IO_ERR_LOGIC);)

#define TEST_OBJLNK_FAIL(Str, ...)                                             \
    TEST_OBJLNK_COMMON(Str,                                                    \
                       fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_OBJLNK;  \
                       ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, \
                                                          &value, &path),      \
                                 FLUF_IO_ERR_FORMAT);                          \
                       ASSERT_NULL(value); ASSERT_NULL(path);)

AVS_UNIT_TEST(text_in, objlnk) {
    TEST_OBJLNK(0, 0);
    TEST_OBJLNK(1, 0);
    TEST_OBJLNK(0, 1);
    TEST_OBJLNK(1, 65535);
    TEST_OBJLNK(65535, 1);
    TEST_OBJLNK(65535, 65535);
    TEST_OBJLNK_FAIL("65536:1");
    TEST_OBJLNK_FAIL("1:65536");
    TEST_OBJLNK_FAIL("0: 0");
    TEST_OBJLNK_FAIL("0 :0");
    TEST_OBJLNK_FAIL(" 0:0");
    TEST_OBJLNK_FAIL("0:0 ");
    TEST_OBJLNK_FAIL("");
    TEST_OBJLNK_FAIL("0");
    TEST_OBJLNK_FAIL("wat");
    TEST_OBJLNK_FAIL("0:wat");
    TEST_OBJLNK_FAIL("wat:0");
}

AVS_UNIT_TEST(text_in, objlnk_in_parts) {
    char data_1[] = "6553";
    char data_2[] = "5:2137";
    TEST_ENV(data_1, MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_OBJLNK;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_OBJLNK);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_NULL(value);

    // second feed
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_2, strlen(data_2), true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_OBJLNK);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->objlnk.oid, 65535);
    ASSERT_EQ(value->objlnk.iid, 2137);

    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(text_in, 8_bytes_in) {
    static char data_in[] = "AgEDBw==";
    static char data_out[] = "\x02\x01\x03\x07";
    TEST_ENV(data_in, MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(data_out) - 1);
    ASSERT_EQ(value->bytes_or_string.offset, 0);
    ASSERT_EQ(value->bytes_or_string.full_length_hint, sizeof(data_out) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(text_in, 16_bytes_in) {
    static char data_in[] = "AgEDB/8AMSUkJicoKTAxAA==";
    static char data_out[] =
            "\x02\x01\x03\x07\xff\x00\x31\x25\x24\x26\x27\x28\x29\x30\x31\x00";
    TEST_ENV(data_in, MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(data_out) - 1);
    ASSERT_EQ(value->bytes_or_string.offset, 0);
    ASSERT_EQ(value->bytes_or_string.full_length_hint, sizeof(data_out) - 1);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out);
}

AVS_UNIT_TEST(text_in, 16_bytes_in_parts) {
    static char data_in_1[] = "AgEDB";
    static char data_in_2[] = "/8AM";
    static char data_in_3[] = "SU";
    static char data_in_4[] = "kJicoKTAxAA==";
    static char data_out_1[] = "\x02\x01\x03";
    static char data_out_2[] = "\x07\xff\x00";
    static char data_out_3[] = "\x31\x25\x24\x26\x27\x28\x29\x30\x31\x00";

    // first feed
    TEST_ENV(data_in_1, MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(data_out_1) - 1);
    ASSERT_EQ(value->bytes_or_string.offset, 0);
    ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out_1);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);

    // second feed
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_in_2,
                                          sizeof(data_in_2) - 1, false));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(data_out_2) - 1);
    ASSERT_EQ(value->bytes_or_string.offset, sizeof(data_out_1) - 1);
    ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out_2);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);

    // third feed - this feed is too small
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_in_3,
                                          sizeof(data_in_3) - 1, false));
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_NULL(value);

    // make sure that we need the next payload
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);

    // fourth feed
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_in_4,
                                          sizeof(data_in_4) - 1, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(data_out_3) - 1);
    ASSERT_EQ(value->bytes_or_string.offset,
              sizeof(data_out_1) + sizeof(data_out_2) - 2);
    ASSERT_EQ(value->bytes_or_string.full_length_hint,
              sizeof(data_out_1) + sizeof(data_out_2) + sizeof(data_out_3) - 3);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out_3);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

#define FIRST_FEED_WITH_UP_TO_3_CHARS(TestName, DataIn1, DataIn2)              \
    AVS_UNIT_TEST(text_in, TestName) {                                         \
        static char data_in_1[] = DataIn1;                                     \
        static char data_in_2[] = DataIn2;                                     \
        static char data_out[] = "\x02\x01\x03\x07\xff\x00\x31\x25\x24\x26"    \
                                 "\x27\x28\x29\x30\x31\x00";                   \
                                                                               \
        TEST_ENV(data_in_1, MAKE_TEST_RESOURCE_PATH(5), false);                \
        fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;                  \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value,        \
                                           &path),                             \
                  FLUF_IO_WANT_NEXT_PAYLOAD);                                  \
        ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);                         \
        ASSERT_TRUE(                                                           \
                fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5))); \
        ASSERT_NULL(value);                                                    \
                                                                               \
        ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_in_2,                 \
                                              sizeof(data_in_2) - 1, true));   \
        ASSERT_OK(                                                             \
                fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path)); \
        ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);                         \
        ASSERT_TRUE(                                                           \
                fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5))); \
        ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(data_out) - 1);  \
        ASSERT_EQ(value->bytes_or_string.offset, 0);                           \
        ASSERT_EQ(value->bytes_or_string.full_length_hint,                     \
                  sizeof(data_out) - 1);                                       \
        ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out);                \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value,        \
                                           &path),                             \
                  FLUF_IO_EOF);                                                \
    }

#define FIRST_FEED_WITH_MORE_THAN_3_CHARS(TestName, DataIn1, DataIn2,          \
                                          DataOut1, DataOut2)                  \
    AVS_UNIT_TEST(text_in, TestName) {                                         \
        static char data_in_1[] = DataIn1;                                     \
        static char data_in_2[] = DataIn2;                                     \
        static char data_out_1[] = DataOut1;                                   \
        static char data_out_2[] = DataOut2;                                   \
                                                                               \
        TEST_ENV(data_in_1, MAKE_TEST_RESOURCE_PATH(5), false);                \
        fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;                  \
        ASSERT_OK(                                                             \
                fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path)); \
        ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);                         \
        ASSERT_TRUE(                                                           \
                fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5))); \
        ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);                         \
        ASSERT_TRUE(                                                           \
                fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5))); \
        ASSERT_EQ(value->bytes_or_string.chunk_length,                         \
                  sizeof(data_out_1) - 1);                                     \
        ASSERT_EQ(value->bytes_or_string.offset, 0);                           \
        ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);                 \
        ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out_1);              \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value,        \
                                           &path),                             \
                  FLUF_IO_WANT_NEXT_PAYLOAD);                                  \
                                                                               \
        ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_in_2,                 \
                                              sizeof(data_in_2) - 1, true));   \
        ASSERT_OK(                                                             \
                fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path)); \
        ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);                         \
        ASSERT_TRUE(                                                           \
                fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5))); \
        ASSERT_EQ(value->bytes_or_string.chunk_length,                         \
                  sizeof(data_out_2) - 1);                                     \
        ASSERT_EQ(value->bytes_or_string.offset, sizeof(data_out_1) - 1);      \
        ASSERT_EQ(value->bytes_or_string.full_length_hint,                     \
                  sizeof(data_out_1) + sizeof(data_out_2) - 2);                \
        ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out_2);              \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value,        \
                                           &path),                             \
                  FLUF_IO_EOF);                                                \
    }

FIRST_FEED_WITH_UP_TO_3_CHARS(zero_char_then_rest,
                              "",
                              "AgEDB/8AMSUkJicoKTAxAA==")
FIRST_FEED_WITH_UP_TO_3_CHARS(one_char_then_rest,
                              "A",
                              "gEDB/8AMSUkJicoKTAxAA==")
FIRST_FEED_WITH_UP_TO_3_CHARS(two_chars_then_rest,
                              "Ag",
                              "EDB/8AMSUkJicoKTAxAA==")
FIRST_FEED_WITH_UP_TO_3_CHARS(three_chars_then_rest,
                              "AgE",
                              "DB/8AMSUkJicoKTAxAA==")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(four_chars_then_rest,
                                  "AgED",
                                  "B/8AMSUkJicoKTAxAA==",
                                  "\x02\x01\x03",
                                  "\x07\xff\x00\x31\x25\x24\x26"
                                  "\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(five_chars_then_rest,
                                  "AgEDB",
                                  "/8AMSUkJicoKTAxAA==",
                                  "\x02\x01\x03",
                                  "\x07\xff\x00\x31\x25\x24\x26"
                                  "\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(six_chars_then_rest,
                                  "AgEDB/",
                                  "8AMSUkJicoKTAxAA==",
                                  "\x02\x01\x03",
                                  "\x07\xff\x00\x31\x25\x24\x26"
                                  "\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(seven_chars_then_rest,
                                  "AgEDB/8",
                                  "AMSUkJicoKTAxAA==",
                                  "\x02\x01\x03",
                                  "\x07\xff\x00\x31\x25\x24\x26"
                                  "\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(eight_chars_then_rest,
                                  "AgEDB/8A",
                                  "MSUkJicoKTAxAA==",
                                  "\x02\x01\x03\x07\xff\x00",
                                  "\x31\x25\x24\x26"
                                  "\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(nine_chars_then_rest,
                                  "AgEDB/8AM",
                                  "SUkJicoKTAxAA==",
                                  "\x02\x01\x03\x07\xff\x00",
                                  "\x31\x25\x24\x26"
                                  "\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(ten_chars_then_rest,
                                  "AgEDB/8AMS",
                                  "UkJicoKTAxAA==",
                                  "\x02\x01\x03\x07\xff\x00",
                                  "\x31\x25\x24\x26"
                                  "\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(eleven_chars_then_rest,
                                  "AgEDB/8AMSU",
                                  "kJicoKTAxAA==",
                                  "\x02\x01\x03\x07\xff\x00",
                                  "\x31\x25\x24\x26"
                                  "\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(twelve_chars_then_rest,
                                  "AgEDB/8AMSUk",
                                  "JicoKTAxAA==",
                                  "\x02\x01\x03\x07\xff\x00\x31\x25\x24",
                                  "\x26\x27\x28\x29\x30\x31\x00")
FIRST_FEED_WITH_MORE_THAN_3_CHARS(thirteen_chars_then_rest,
                                  "AgEDB/8AMSUkJ",
                                  "icoKTAxAA==",
                                  "\x02\x01\x03\x07\xff\x00\x31\x25\x24",
                                  "\x26\x27\x28\x29\x30\x31\x00")

#define IN_INDEX_TO_OUT_INDEX(i) (3 * (((i) -4) / 4))
AVS_UNIT_TEST(text_in, provide_chars_one_by_one) {
    static char data_in[] = "ITcEIGkBAgMEBQYHCAkKCwwOD//+/fz7+vn49/b19PPy8fA=";
    static char data_out[] = "\x21\x37\x04\x20\x69\x01\x02\x03\x04\x05\x06\x07"
                             "\x08\x09\x0A\x0B\x0C\x0E\x0F\xFF\xFE\xFD\xFC\xFB"
                             "\xFA\xF9\xF8\xF7\xF6\xF5\xF4\xF3\xF2\xF1\xF0";

    fluf_io_in_ctx_t ctx;
    ASSERT_OK(fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                  &(MAKE_TEST_RESOURCE_PATH(5)),
                                  FLUF_COAP_FORMAT_PLAINTEXT));
    const fluf_res_value_t *value = NULL;
    const fluf_uri_path_t *path = NULL;

    int result = 0;
    size_t i = 0;
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    while (i < sizeof(data_in) - 1) {
        ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, &data_in[i++], 1, false));
        result = fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path);
        ASSERT_TRUE(
                fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
        path = NULL;
        if (i % 4 != 0) {
            ASSERT_EQ(result, FLUF_IO_WANT_NEXT_PAYLOAD);
            ASSERT_NULL(value);
        } else {
            ASSERT_OK(result);
            ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
            ASSERT_EQ(value->bytes_or_string.offset, IN_INDEX_TO_OUT_INDEX(i));
            ASSERT_EQ(value->bytes_or_string.chunk_length,
                      i == sizeof(data_in) - 1 ? 2 : 3);
            ASSERT_EQ_BYTES_SIZED(value->bytes_or_string.data,
                                  &data_out[IN_INDEX_TO_OUT_INDEX(i)],
                                  value->bytes_or_string.chunk_length);
        }
    }
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, "", 0, true));
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path), 0);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(text_in, feeding_after_padded) {
    static char data_in_1[] = "BB==";
    static char data_in_2[] = "BB==";
    static char data_out[] = "\x04";

    TEST_ENV(data_in_1, MAKE_TEST_RESOURCE_PATH(5), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(data_out) - 1);
    ASSERT_EQ(value->bytes_or_string.offset, 0);
    ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, data_out);
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, data_in_2, strlen(data_in_2),
                                          false));
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(text_in, padding_after_padding) {
    static char data_in[] = "AA==AA==";

    TEST_ENV(data_in, MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(text_in, test_diambiguation) {
    static char data_in[] = "AgEDB/8AMSUkJicoKTAxAA==";
    TEST_ENV(data_in, MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_ANY;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_ANY);
}

AVS_UNIT_TEST(text_in, int_no_data_with_payload_finished) {
    TEST_ENV("", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
    ASSERT_NULL(value);
    ASSERT_NULL(path);
}

AVS_UNIT_TEST(text_in, uint_no_data_with_payload_finished) {
    TEST_ENV("", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_UINT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
    ASSERT_NULL(value);
    ASSERT_NULL(path);
}

AVS_UNIT_TEST(text_in, bool_no_data_with_payload_finished) {
    TEST_ENV("", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BOOL;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
    ASSERT_NULL(value);
    ASSERT_NULL(path);
}

AVS_UNIT_TEST(text_in, objlnk_no_data_with_payload_finished) {
    TEST_ENV("", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_OBJLNK;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
    ASSERT_NULL(value);
    ASSERT_NULL(path);
}

AVS_UNIT_TEST(text_in, time_no_data_with_payload_finished) {
    TEST_ENV("", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_TIME;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
    ASSERT_NULL(value);
    ASSERT_NULL(path);
}

AVS_UNIT_TEST(text_in, bytes_no_data_with_payload_finished) {
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

AVS_UNIT_TEST(text_in, string_no_data_with_payload_finished) {
    TEST_ENV("", MAKE_TEST_RESOURCE_PATH(5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_STRING;
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
