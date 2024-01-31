/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_memory.h>
#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include "bigdata.h"

#define TEST_ENV(Data, Path, PayloadFinished)                                \
    fluf_io_in_ctx_t ctx;                                                    \
    ASSERT_OK(fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,     \
                                  &(Path), FLUF_COAP_FORMAT_OMA_LWM2M_TLV)); \
    const fluf_res_value_t *value = NULL;                                    \
    const fluf_uri_path_t *path = NULL;                                      \
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, Data, sizeof(Data) - 1,      \
                                          PayloadFinished))

static const fluf_uri_path_t TEST_INSTANCE_PATH =
        _FLUF_URI_PATH_INITIALIZER(3, 4, FLUF_ID_INVALID, FLUF_ID_INVALID, 2);

#define MAKE_TEST_RESOURCE_PATH(Rid)                              \
    (FLUF_MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[FLUF_ID_OID], \
                             TEST_INSTANCE_PATH.ids[FLUF_ID_IID], (Rid)))

#define TLV_BYTES_TEST(Name, Path, Header, Data)                               \
    AVS_UNIT_TEST(tlv_in_bytes, Name) {                                        \
        TEST_ENV(Header Data, TEST_INSTANCE_PATH, true);                       \
        fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;                  \
        ASSERT_OK(                                                             \
                fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path)); \
        ASSERT_TRUE(fluf_uri_path_equal(path, &(Path)));                       \
        ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(Data) - 1);      \
        ASSERT_EQ_BYTES(value->bytes_or_string.data, Data);                    \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value,        \
                                           &path),                             \
                  FLUF_IO_EOF);                                                \
        AVS_UNIT_ASSERT_NULL(value);                                           \
        AVS_UNIT_ASSERT_NULL(path);                                            \
    }

// 3 bits for length - <=7
TLV_BYTES_TEST(len3b_id8b, MAKE_TEST_RESOURCE_PATH(0), "\xC7\x00", "1234567")
TLV_BYTES_TEST(len3b_id16b,
               MAKE_TEST_RESOURCE_PATH(42000),
               "\xE7\xA4\x10",
               "1234567")
TLV_BYTES_TEST(len8b_id8b,
               MAKE_TEST_RESOURCE_PATH(255),
               "\xC8\xFF\x08",
               "12345678")
TLV_BYTES_TEST(len8b_id16b,
               MAKE_TEST_RESOURCE_PATH(65534),
               "\xE8\xFF\xFE\x08",
               "12345678")

TLV_BYTES_TEST(len16b_id8b,
               MAKE_TEST_RESOURCE_PATH(42),
               "\xD0\x2A\x03\xE8",
               DATA1kB)
TLV_BYTES_TEST(len16b_id16b,
               MAKE_TEST_RESOURCE_PATH(42420),
               "\xF0\xA5\xB4\x03\xE8",
               DATA1kB)

TLV_BYTES_TEST(len24b_id8b,
               MAKE_TEST_RESOURCE_PATH(69),
               "\xD8\x45\x01\x86\xA0",
               DATA100kB)
TLV_BYTES_TEST(len24b_id16b,
               MAKE_TEST_RESOURCE_PATH(258),
               "\xF8\x01\x02\x01\x86\xA0",
               DATA100kB)

#undef TLV_BYTES_TEST

AVS_UNIT_TEST(tlv_in_bytes, id_too_short) {
    TEST_ENV("\xE7", FLUF_MAKE_OBJECT_PATH(3), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
}

AVS_UNIT_TEST(tlv_in_bytes, id_too_short_with_payload_finished) {
    TEST_ENV("\xE7", FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(tlv_in_bytes, length_too_short) {
    TEST_ENV("\xF8\x01\x02\x01\x86", FLUF_MAKE_OBJECT_PATH(3), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
}

AVS_UNIT_TEST(tlv_in_bytes, length_too_short_with_payload_finished) {
    TEST_ENV("\xF8\x01\x02\x01\x86", FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(tlv_in_bytes, multiple_resource_entries) {
    // [ RID(42)="0123", RID(69)="0123456", RID(22)="01234" ]
    char DATA[] = "\xC4\x2A"
                  "0123"
                  "\xC7\x45"
                  "0123456"
                  "\xC5\x16"
                  "01234";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 42)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 4);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "0123");
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 69)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 7);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "0123456");
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 22)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 5);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "01234");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_bytes, premature_end) {
    static char DATA[] = "\xC7\x2A"
                         "012";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
}

AVS_UNIT_TEST(tlv_in_bytes, premature_end_with_payload_finished) {
    static char DATA[] = "\xC7\x2A"
                         "012";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(tlv_in_bytes, premature_end_with_feeding) {
    static char DATA[] = "\xC7\x2A"
                         "012";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 3);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "012");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, "3456", 4, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 4);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "3456");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_bytes, no_data) {
    TEST_ENV("", FLUF_MAKE_OBJECT_PATH(3), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    AVS_UNIT_ASSERT_NULL(value);
    AVS_UNIT_ASSERT_NULL(path);
}

AVS_UNIT_TEST(tlv_in_bytes, no_data_with_payload_finished) {
    TEST_ENV("", FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
    AVS_UNIT_ASSERT_NULL(value);
    AVS_UNIT_ASSERT_NULL(path);
}

AVS_UNIT_TEST(tlv_in_types, string_ok) {
    // RID(01)="Hello, world!"
    static char DATA[] = "\xC8\x01\x0D"
                         "Hello, world!";
    TEST_ENV(DATA, FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_STRING;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 13);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "Hello, world!");
}

#define TEST_NUM_IMPL(Name, Type, Suffix, TypeBitmask, Num, Data)              \
    AVS_UNIT_TEST(tlv_in_types, Name) {                                        \
        TEST_ENV(Data, TEST_INSTANCE_PATH, true);                              \
        fluf_data_type_t type_bitmask = TypeBitmask;                           \
        ASSERT_OK(                                                             \
                fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path)); \
        ASSERT_EQ(value->Suffix, (Type) Num);                                  \
    }

#define TEST_NUM_FAIL_IMPL(Name, Type, Suffix, TypeBitmask, Data)       \
    AVS_UNIT_TEST(tlv_in_types, Name) {                                 \
        TEST_ENV(Data, TEST_INSTANCE_PATH, true);                       \
        fluf_data_type_t type_bitmask = TypeBitmask;                    \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, \
                                           &path),                      \
                  FLUF_IO_ERR_FORMAT);                                  \
    }

#define TEST_NUM(Type, Suffix, TypeBitmask, Num, Data)                        \
    TEST_NUM_IMPL(AVS_CONCAT(Suffix##_, __LINE__), Type, Suffix, TypeBitmask, \
                  Num, Data)
#define TEST_NUM_FAIL(Type, Suffix, TypeBitmask, Data)                    \
    TEST_NUM_FAIL_IMPL(AVS_CONCAT(Suffix##fail_, __LINE__), Type, Suffix, \
                       TypeBitmask, Data)

#define TEST_INT64(Num, Data) \
    TEST_NUM(int64_t, int_value, FLUF_DATA_TYPE_INT, Num##LL, Data)

#define TEST_INT64_FAIL(Data) \
    TEST_NUM_FAIL(int64_t, int_value, FLUF_DATA_TYPE_INT, Data)
TEST_INT64_FAIL("\xC0\x01"
                "")
TEST_INT64(42,
           "\xC1\x01"
           "\x2A")
TEST_INT64(4242,
           "\xC2\x01"
           "\x10\x92")
TEST_INT64_FAIL("\xC3\x01"
                "\x06\x79\x32")
TEST_INT64(424242,
           "\xC4\x01"
           "\x00\x06\x79\x32")
TEST_INT64(42424242,
           "\xC4\x01"
           "\x02\x87\x57\xB2")
TEST_INT64((int32_t) 4242424242,
           "\xC4\01"
           "\xFC\xDE\x41\xB2")
TEST_INT64(4242424242,
           "\xC8\x01\x08"
           "\x00\x00\x00\x00\xFC\xDE\x41\xB2")
TEST_INT64_FAIL("\xC5\x01"
                "\x62\xC6\xD1\xA9\xB2")
TEST_INT64(424242424242,
           "\xC8\x01\x08"
           "\x00\x00\x00\x62\xC6\xD1\xA9\xB2")
TEST_INT64_FAIL("\xC6\x01"
                "\x26\x95\xA9\xE6\x49\xB2")
TEST_INT64(42424242424242,
           "\xC8\x01\x08"
           "\x00\x00\x26\x95\xA9\xE6\x49\xB2")
TEST_INT64_FAIL("\xC8\x01\x07"
                "\x0F\x12\x76\x5D\xF4\xC9\xB2")
TEST_INT64(4242424242424242,
           "\xC8\x01\x08"
           "\x00\x0F\x12\x76\x5D\xF4\xC9\xB2")
TEST_INT64(424242424242424242,
           "\xC8\x01\x08"
           "\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")
TEST_INT64_FAIL("\xC8\x01\x09"
                "\x00\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")

TEST_INT64_FAIL(
        "\xC8\x01\x10"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")

AVS_UNIT_TEST(tlv_in_types, int64_two_feeds) {
    static char DATA[] = "\xC8\x01\x08"
                         "\x05\xE3\x36";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    AVS_UNIT_ASSERT_NULL(value);
    ASSERT_EQ_BYTES_SIZED(path, &MAKE_TEST_RESOURCE_PATH(1),
                          sizeof(FLUF_MAKE_ROOT_PATH()));
    ASSERT_OK(
            fluf_io_in_ctx_feed_payload(&ctx, "\x3C\xB3\x9E\xC9\xB2", 5, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->int_value, 424242424242424242);
}

#define TEST_UINT64(Num, Data) \
    TEST_NUM(uint64_t, uint_value, FLUF_DATA_TYPE_UINT, Num##ULL, Data)
#define TEST_UINT64_FAIL(Data) \
    TEST_NUM_FAIL(uint64_t, uint_value, FLUF_DATA_TYPE_UINT, Data)

TEST_UINT64_FAIL("\xC0\x01"
                 "")
TEST_UINT64(42,
            "\xC1\x01"
            "\x2A")
TEST_UINT64_FAIL("\xC3\x01"
                 "\x06\x79\x32")
TEST_UINT64(4294967295,
            "\xC4\x01"
            "\xFF\xFF\xFF\xFF")
TEST_UINT64_FAIL("\xC5\x01"
                 "\x01\x00\x00\x00\x00")
TEST_UINT64(4294967296,
            "\xC8\x01\x08"
            "\x00\x00\x00\x01\x00\x00\x00\x00")
TEST_UINT64(18446744073709551615,
            "\xC8\x01\x08"
            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF")

TEST_UINT64_FAIL(
        "\xC8\x01\x10"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")

AVS_UNIT_TEST(tlv_in_types, uint64_two_feeds) {
    static char DATA[] = "\xC8\x01\x08"
                         "\x05\xE3\x36";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_UINT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    AVS_UNIT_ASSERT_NULL(value);
    ASSERT_EQ_BYTES_SIZED(path, &MAKE_TEST_RESOURCE_PATH(1),
                          sizeof(FLUF_MAKE_ROOT_PATH()));
    ASSERT_OK(
            fluf_io_in_ctx_feed_payload(&ctx, "\x3C\xB3\x9E\xC9\xB2", 5, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->uint_value, 424242424242424242);
}

#define TEST_DOUBLE(Num, Data) \
    TEST_NUM(double, double_value, FLUF_DATA_TYPE_DOUBLE, Num, Data)

#define TEST_DOUBLE_FAIL(Data) \
    TEST_NUM_FAIL(double, double, FLUF_DATA_TYPE_DOUBLE, Data)

TEST_DOUBLE_FAIL("\xC0\x01"
                 "")
TEST_DOUBLE_FAIL("\xC1\x01"
                 "\x3F")
TEST_DOUBLE_FAIL("\xC2\x01"
                 "\x3F\x80")
TEST_DOUBLE_FAIL("\xC3\x01"
                 "\x3F\x80\x00")
TEST_DOUBLE(1.0,
            "\xC4\x01"
            "\x3F\x80\x00\x00")
TEST_DOUBLE(-42.0e3,
            "\xC4\x01"
            "\xC7\x24\x10\x00")
TEST_DOUBLE_FAIL("\xC5\x01"
                 "\x3F\xF0\x00\x00\x00")
TEST_DOUBLE_FAIL("\xC6\x01"
                 "\x3F\xF0\x00\x00\x00\x00")
TEST_DOUBLE_FAIL("\xC7\x01"
                 "\x3F\xF0\x00\x00\x00\x00\x00")
TEST_DOUBLE(1.0,
            "\xC8\x01\x08"
            "\x3F\xF0\x00\x00\x00\x00\x00\x00")
TEST_DOUBLE(1.1,
            "\xC8\x01\x08"
            "\x3F\xF1\x99\x99\x99\x99\x99\x9A")
TEST_DOUBLE(-42.0e3,
            "\xC8\x01\x08"
            "\xC0\xE4\x82\x00\x00\x00\x00\x00")
TEST_DOUBLE_FAIL("\xC8\x01\x09"
                 "\xC0\xE4\x82\x00\x00\x00\x00\x00\x00")

AVS_UNIT_TEST(tlv_in_types, double_two_feeds) {
    static char DATA[] = "\xC8\x01\x08"
                         "\x3F\xF1\x99";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_DOUBLE;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    AVS_UNIT_ASSERT_NULL(value);
    ASSERT_EQ_BYTES_SIZED(path, &MAKE_TEST_RESOURCE_PATH(1),
                          sizeof(FLUF_MAKE_ROOT_PATH()));
    ASSERT_OK(
            fluf_io_in_ctx_feed_payload(&ctx, "\x99\x99\x99\x99\x9A", 5, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->double_value, 1.1);
}

#define TEST_BOOL_IMPL(Name, Value, Data, TypeBitmask)                         \
    AVS_UNIT_TEST(tlv_in_types, Name) {                                        \
        TEST_ENV(Data, TEST_INSTANCE_PATH, true);                              \
        fluf_data_type_t type_bitmask = TypeBitmask;                           \
        ASSERT_OK(                                                             \
                fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path)); \
        ASSERT_EQ(!!(Value), value->bool_value);                               \
    }

#define TEST_BOOL_FAIL_IMPL(Name, Data, TypeBitmask)                    \
    AVS_UNIT_TEST(tlv_in_types, Name) {                                 \
        TEST_ENV(Data, TEST_INSTANCE_PATH, true);                       \
        fluf_data_type_t type_bitmask = TypeBitmask;                    \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, \
                                           &path),                      \
                  FLUF_IO_ERR_FORMAT);                                  \
    }

#define TEST_BOOL(Value, Data)                               \
    TEST_BOOL_IMPL(AVS_CONCAT(bool_, __LINE__), Value, Data, \
                   FLUF_DATA_TYPE_BOOL)
#define TEST_BOOL_FAIL(Data) \
    TEST_BOOL_FAIL_IMPL(AVS_CONCAT(bool_, __LINE__), Data, FLUF_DATA_TYPE_BOOL)

TEST_BOOL_FAIL("\xC0\x01"
               "")
TEST_BOOL(false,
          "\xC1\x01"
          "\0")
TEST_BOOL(true,
          "\xC1\x01"
          "\1")
TEST_BOOL_FAIL("\xC1\x01"
               "\2")
TEST_BOOL_FAIL("\xC2\x01"
               "\0\0")

#define TEST_OBJLNK_IMPL(Name, TypeBitmask, Oid, Iid, Data)                    \
    AVS_UNIT_TEST(tlv_in_types, Name) {                                        \
        TEST_ENV(Data, TEST_INSTANCE_PATH, true);                              \
        fluf_data_type_t type_bitmask = TypeBitmask;                           \
        ASSERT_OK(                                                             \
                fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path)); \
        ASSERT_EQ(value->objlnk.oid, Oid);                                     \
        ASSERT_EQ(value->objlnk.iid, Iid);                                     \
    }

#define TEST_OBJLNK_FAIL_IMPL(Name, TypeBitmask, Data)                  \
    AVS_UNIT_TEST(tlv_in_types, Name) {                                 \
        TEST_ENV(Data, TEST_INSTANCE_PATH, true);                       \
        fluf_data_type_t type_bitmask = TypeBitmask;                    \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, \
                                           &path),                      \
                  FLUF_IO_ERR_FORMAT);                                  \
    }

#define TEST_OBJLNK(...)                                                   \
    TEST_OBJLNK_IMPL(AVS_CONCAT(objlnk_, __LINE__), FLUF_DATA_TYPE_OBJLNK, \
                     __VA_ARGS__)
#define TEST_OBJLNK_FAIL(...)                            \
    TEST_OBJLNK_FAIL_IMPL(AVS_CONCAT(objlnk_, __LINE__), \
                          FLUF_DATA_TYPE_OBJLNK, __VA_ARGS__)

TEST_OBJLNK_FAIL("\xC0\x01"
                 "")
TEST_OBJLNK_FAIL("\xC1\x01"
                 "\x00")
TEST_OBJLNK_FAIL("\xC2\x01"
                 "\x00\x00")
TEST_OBJLNK_FAIL("\xC3\x01"
                 "\x00\x00\x00")
TEST_OBJLNK(0,
            0,
            "\xC4\x01"
            "\x00\x00\x00\x00")
TEST_OBJLNK(1,
            0,
            "\xC4\x01"
            "\x00\x01\x00\x00")
TEST_OBJLNK(0,
            1,
            "\xC4\x01"
            "\x00\x00\x00\x01")
TEST_OBJLNK(1,
            65535,
            "\xC4\x01"
            "\x00\x01\xFF\xFF")
TEST_OBJLNK(65535,
            1,
            "\xC4\x01"
            "\xFF\xFF\x00\x01")
TEST_OBJLNK(65535,
            65535,
            "\xC4\x01"
            "\xFF\xFF\xFF\xFF")
TEST_OBJLNK_FAIL("\xC5\x01"
                 "\xFF\xFF\xFF\xFF\xFF")

AVS_UNIT_TEST(tlv_in_types, objlnk_two_feeds) {
    static char DATA[] = "\xC4\x01"
                         "\x00";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_OBJLNK;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
    AVS_UNIT_ASSERT_NULL(value);
    ASSERT_EQ_BYTES_SIZED(path, &MAKE_TEST_RESOURCE_PATH(1),
                          sizeof(FLUF_MAKE_ROOT_PATH()));
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, "\x01\xFF\xFF", 3, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->objlnk.oid, 1);
    ASSERT_EQ(value->objlnk.iid, 65535);
}

AVS_UNIT_TEST(tlv_in_types, time_ok) {
    static char DATA[] = "\xC8\x01\x08"
                         "\x00\x00\x00\x00\x42\x4E\xF4\x5C";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_TIME;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(value->time_value, 1112470620);
}

AVS_UNIT_TEST(tlv_in_types, no_value) {
    static char DATA[] = "\xC0\x01";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_ANY;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_ANY);
    type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_NULL(value);
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_NULL(value->bytes_or_string.data);
    ASSERT_EQ(value->bytes_or_string.offset, 0);
    ASSERT_EQ(value->bytes_or_string.chunk_length, 0);
    ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);
}

AVS_UNIT_TEST(tlv_in_types, no_value_with_payload_finished) {
    static char DATA[] = "\xC0\x01";
    TEST_ENV(DATA, TEST_INSTANCE_PATH, true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_ANY;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_ANY);
    type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_NULL(value);
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_BYTES);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_NULL(value->bytes_or_string.data);
    ASSERT_EQ(value->bytes_or_string.offset, 0);
    ASSERT_EQ(value->bytes_or_string.chunk_length, 0);
    ASSERT_EQ(value->bytes_or_string.full_length_hint, 0);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_path, typical_payload_for_create_without_iid) {
    static char DATA[] = "\xC7\x00"
                         "1234567";
    TEST_ENV(DATA, FLUF_MAKE_OBJECT_PATH(42), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_STRING;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));

    ASSERT_TRUE(fluf_uri_path_equal(
            path, &FLUF_MAKE_RESOURCE_PATH(42, FLUF_ID_INVALID, 0)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 7);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "1234567");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_path, payload_write_on_instance_with_rids_only) {
    // [ RID(1)=10, RID(2)=10, RID(3)=10 ]
    static char DATA[] = "\xc1\x01\x0a\xc1\x02\x0b\xc1\x03\x0c";
    TEST_ENV(DATA, FLUF_MAKE_INSTANCE_PATH(3, 4), true);

    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_EQ(value->int_value, 10);

    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 2)));
    ASSERT_EQ(value->int_value, 11);

    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 3)));
    ASSERT_EQ(value->int_value, 12);

    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_path,
              payload_write_on_instance_with_rids_uri_iid_mismatch) {
    // IID(5, [ RID(1)=10 ])
    static char DATA[] = "\x03\x05\xc1\x01\x0a";
    TEST_ENV(DATA, FLUF_MAKE_INSTANCE_PATH(3, 4), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(tlv_in_path, fail_on_path_with_invalid_iid) {
    // IID(ANJAY_ID_INVALID, [ RID(1)=1 ])
    static char DATA[] = "\x23\xff\xff\xc1\x01\x0a";
    TEST_ENV(DATA, FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(tlv_in_path, fail_on_path_with_invalid_rid) {
    // IID(5, [ RID(ANJAY_ID_INVALID)=10 ])
    static char DATA[] = "\x04\x05\xe1\xff\xff\x0a";
    TEST_ENV(DATA, FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(tlv_in_path, fail_on_path_with_invalid_riid) {
    // RIID=ANJAY_ID_INVALID
    static char DATA[] = "\x61\xff\xff\x0a";
    TEST_ENV(DATA, FLUF_MAKE_RESOURCE_PATH(5, 0, 1), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(tlv_in_path, payload_write_on_instance_with_rids) {
    // IID(4, [ RID(1)=10, RID(2)=11 ])
    static char DATA[] = "\x06\x04\xc1\x01\x0a\xc1\x02\x0b";
    TEST_ENV(DATA, FLUF_MAKE_INSTANCE_PATH(3, 4), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_EQ(value->int_value, 10);
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 2)));
    ASSERT_EQ(value->int_value, 11);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_path, payload_write_on_resource_with_riids_only) {
    // [ RIID(1)=10, RIID(2)=11, RIID(3)=112 ]
    static char DATA[] = "\x41\x01\x0a\x41\x02\x0b\x41\x03\x0c";
    TEST_ENV(DATA, FLUF_MAKE_RESOURCE_PATH(3, 4, 5), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(
            path, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 1)));
    ASSERT_EQ(value->int_value, 10);
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(
            path, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 2)));
    ASSERT_EQ(value->int_value, 11);
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(
            path, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 3)));
    ASSERT_EQ(value->int_value, 12);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_path, payload_write_on_resource_with_riids) {
    // [ RID(5)=[ RIID(1)=10, RIID(2)=11] ]
    static char DATA[] = "\x86\x05\x41\x01\x0a\x41\x02\x0b";
    TEST_ENV(DATA, FLUF_MAKE_INSTANCE_PATH(3, 4), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(
            path, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 1)));
    ASSERT_EQ(value->int_value, 10);
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(
            path, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 2)));
    ASSERT_EQ(value->int_value, 11);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_path, payload_write_on_instance_with_resource_with_riids) {
    // IID(4, [ RID(5)=[ RIID(1)=10, RIID(2)=11] ])
    static char DATA[] = "\x08\x04\x08\x86\x05\x41\x01\x0a\x41\x02\x0b";
    TEST_ENV(DATA, FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(
            path, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 1)));
    ASSERT_EQ(value->int_value, 10);
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(
            path, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 2)));
    ASSERT_EQ(value->int_value, 11);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_path, empty_instances_list) {
    // [ Instance(1), Instance(2) ]
    static char DATA[] = "\x00\x01\x00\x02";
    TEST_ENV(DATA, FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_ANY;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_NULL);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_INSTANCE_PATH(3, 1)));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_NULL);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_INSTANCE_PATH(3, 2)));
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_general_tests,
              feed_payload_with_chunk_of_size_zero_with_finished_set_to_true) {
    // [ RID(1)=10 ]
    static char DATA[] = "\xc1\x01\x0a";

    // payload_finished flag set to false
    TEST_ENV(DATA, FLUF_MAKE_INSTANCE_PATH(3, 4), false);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_EQ(value->int_value, 10);

    // below call to fluf_io_in_ctx_get_entry() should return
    // FLUF_IO_WANT_NEXT_PAYLOAD as last feed was with payload_finished flag set
    // to false
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);

    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, "", 0, true));
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_general_tests, check_want_disambiguation) {
    char *in_tlv = "\xC7\x05"
                   "1234567";
    fluf_io_in_ctx_t ctx;
    ASSERT_OK(fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                  &TEST_INSTANCE_PATH,
                                  FLUF_COAP_FORMAT_OMA_LWM2M_TLV));
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value = NULL;
    const fluf_uri_path_t *path = NULL;
    ASSERT_OK(fluf_io_in_ctx_feed_payload(&ctx, in_tlv, 9, true));
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_NULL(value);
    type_bitmask = FLUF_DATA_TYPE_BYTES;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &MAKE_TEST_RESOURCE_PATH(5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 7);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "1234567");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_general_tests, string_in_chunks) {
    // RID(01)="Hello, world!1234567892137Papaj"
    static char DATA1[] = "\xC8\x05\x1F"
                          "Hello, world!";
    static char DATA2[] = "123456789";
    static char DATA3[] = "2137Papaj";

    fluf_io_in_ctx_t ctx;
    ASSERT_OK(fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                  &TEST_INSTANCE_PATH,
                                  FLUF_COAP_FORMAT_OMA_LWM2M_TLV));
    const fluf_res_value_t *value = NULL;
    const fluf_uri_path_t *path = NULL;

    // feed first chunk
    ASSERT_OK(
            fluf_io_in_ctx_feed_payload(&ctx, DATA1, sizeof(DATA1) - 1, false));
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_STRING;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &MAKE_TEST_RESOURCE_PATH(5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, 13);
    ASSERT_EQ(value->bytes_or_string.offset, 0);
    ASSERT_EQ(value->bytes_or_string.full_length_hint,
              sizeof(DATA1) - 1 - 3 + sizeof(DATA2) - 1 + sizeof(DATA3) - 1);
    ASSERT_EQ_BYTES_SIZED(value->bytes_or_string.data, &DATA1[3], 13);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);

    // feed second chunk
    ASSERT_OK(
            fluf_io_in_ctx_feed_payload(&ctx, DATA2, sizeof(DATA2) - 1, false));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &MAKE_TEST_RESOURCE_PATH(5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(DATA2) - 1);
    ASSERT_EQ(value->bytes_or_string.offset, 13);
    ASSERT_EQ(value->bytes_or_string.full_length_hint,
              sizeof(DATA1) - 1 - 3 + sizeof(DATA2) - 1 + sizeof(DATA3) - 1);
    ASSERT_EQ_BYTES_SIZED(value->bytes_or_string.data, DATA2,
                          sizeof(DATA2) - 1);
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_WANT_NEXT_PAYLOAD);

    // feed third chunk
    ASSERT_OK(
            fluf_io_in_ctx_feed_payload(&ctx, DATA3, sizeof(DATA3) - 1, true));
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &MAKE_TEST_RESOURCE_PATH(5)));
    ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(DATA3) - 1);
    ASSERT_EQ(value->bytes_or_string.offset, 13 + sizeof(DATA2) - 1);
    ASSERT_EQ(value->bytes_or_string.full_length_hint,
              sizeof(DATA1) - 1 - 3 + sizeof(DATA2) - 1 + sizeof(DATA3) - 1);
    ASSERT_EQ_BYTES_SIZED(value->bytes_or_string.data, DATA3,
                          sizeof(DATA3) - 1);

    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_general_tests, instance_with_rid_of_different_type) {
    // IID(4, [ RID(5)=10, RID(6)="Hello, world!" ])
    static char DATA[] = "\x08\x04\x13\xC1\x05\x0a\xC8\x06\x0D"
                         "Hello, world!";
    TEST_ENV(DATA, FLUF_MAKE_OBJECT_PATH(3), true);
    fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_INT;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_INT);
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(value->int_value, 10);
    type_bitmask = FLUF_DATA_TYPE_STRING;
    ASSERT_OK(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path));
    ASSERT_TRUE(fluf_uri_path_equal(path, &FLUF_MAKE_RESOURCE_PATH(3, 4, 6)));
    ASSERT_EQ(type_bitmask, FLUF_DATA_TYPE_STRING);
    ASSERT_EQ(value->bytes_or_string.chunk_length, 13);
    ASSERT_EQ_BYTES(value->bytes_or_string.data, "Hello, world!");
    ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path),
              FLUF_IO_EOF);
}

AVS_UNIT_TEST(tlv_in_general_tests, get_entry_count) {
    fluf_io_in_ctx_t ctx;
    ASSERT_OK(fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                  &TEST_INSTANCE_PATH,
                                  FLUF_COAP_FORMAT_OMA_LWM2M_TLV));
    size_t out_count;
    ASSERT_EQ(fluf_io_in_ctx_get_entry_count(&ctx, &out_count),
              FLUF_IO_ERR_FORMAT);
}

#define HEADER_IN_CHUNKS(Header1, Header2, Value)                         \
    HEADER_IN_CHUNKS_TEST(AVS_CONCAT(tlv_in_header_in_chunks_, __LINE__), \
                          Header1, Header2, Value)

#define HEADER_IN_CHUNKS_TEST(Name, Header1, Header2, Value)                   \
    AVS_UNIT_TEST(tlv_in_header_in_chunks, Name) {                             \
        TEST_ENV(Header1, FLUF_MAKE_OBJECT_PATH(3), false);                    \
        fluf_data_type_t type_bitmask = FLUF_DATA_TYPE_BYTES;                  \
        ASSERT_EQ(fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value,        \
                                           &path),                             \
                  FLUF_IO_WANT_NEXT_PAYLOAD);                                  \
        ASSERT_OK(fluf_io_in_ctx_feed_payload(                                 \
                &ctx, Header2 Value, sizeof(Header2 Value) - 1, true));        \
        ASSERT_OK(                                                             \
                fluf_io_in_ctx_get_entry(&ctx, &type_bitmask, &value, &path)); \
        ASSERT_EQ(value->bytes_or_string.chunk_length, sizeof(Value) - 1);     \
        ASSERT_EQ_BYTES(value->bytes_or_string.data, Value);                   \
    }

HEADER_IN_CHUNKS("", "\xC8\x01\x0D", "Hello, world!")
HEADER_IN_CHUNKS("\xC8", "\x01\x0D", "Hello, world!")
HEADER_IN_CHUNKS("\xC8\x01", "\x0D", "Hello, world!")
HEADER_IN_CHUNKS("", "\xF8\x01\x02\x01\x86\xA0", DATA100kB)
HEADER_IN_CHUNKS("\xF8", "\x01\x02\x01\x86\xA0", DATA100kB)
HEADER_IN_CHUNKS("\xF8\x01", "\x02\x01\x86\xA0", DATA100kB)
HEADER_IN_CHUNKS("\xF8\x01\x02", "\x01\x86\xA0", DATA100kB)
HEADER_IN_CHUNKS("\xF8\x01\x02\x01", "\x86\xA0", DATA100kB)
HEADER_IN_CHUNKS("\xF8\x01\x02\x01\x86", "\xA0", DATA100kB)
HEADER_IN_CHUNKS("\xF8\x01\x02\x01\x86\xA0", "", DATA100kB)
