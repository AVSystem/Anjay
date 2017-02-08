/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include <avsystem/commons/unit/memstream.h>
#include <avsystem/commons/unit/test.h>

#include "bigdata.h"
#include "anjay/anjay.h"

#define TEST_ENV(Size) \
    avs_stream_abstract_t *stream = NULL; \
    AVS_UNIT_ASSERT_SUCCESS(avs_unit_memstream_alloc(&stream, Size)); \
    anjay_input_ctx_t *in; \
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_tlv_create(&in, &stream, false));

#define TEST_TEARDOWN do { \
    _anjay_input_ctx_destroy(&in); \
    avs_stream_cleanup(&stream); \
} while (0)

#define TLV_BYTES_TEST_DATA(Header, Data) do { \
    char *buf = (char *) malloc(sizeof(Data) + sizeof(Header)); \
    size_t bytes_read; \
    bool message_finished; \
    AVS_UNIT_ASSERT_SUCCESS( \
            anjay_get_bytes(in, &bytes_read, &message_finished, \
                            buf, sizeof(Data) + sizeof(Header))); \
    AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(Data) - 1); \
    AVS_UNIT_ASSERT_TRUE(message_finished); \
    AVS_UNIT_ASSERT_EQUAL_BYTES(buf, Data); \
    free(buf); \
} while (0)

#define TLV_BYTES_TEST_ID(IdType, Id) do { \
    anjay_id_type_t type; \
    uint16_t id; \
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(in, &type, &id)); \
    AVS_UNIT_ASSERT_EQUAL(type, IdType); \
    AVS_UNIT_ASSERT_EQUAL(id, Id); \
} while (0)

#define TLV_BYTES_TEST(Name, IdType, Id, Header, Data) \
AVS_UNIT_TEST(tlv_in_bytes, Name) { \
    TEST_ENV(sizeof(Data) + sizeof(Header)); \
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, Header Data, \
                                             sizeof(Header Data) - 1)); \
    TLV_BYTES_TEST_DATA(Header, Data); \
    TEST_TEARDOWN; \
} \
\
AVS_UNIT_TEST(tlv_in_bytes, Name##_with_id) { \
    TEST_ENV(sizeof(Data) + sizeof(Header)); \
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, Header Data, \
                                             sizeof(Header Data) - 1)); \
    TLV_BYTES_TEST_ID(IdType, Id); \
    TLV_BYTES_TEST_ID(IdType, Id); \
    TLV_BYTES_TEST_DATA(Header, Data); \
    TEST_TEARDOWN; \
}

// 3 bits for length - <=7
TLV_BYTES_TEST(len3b_id8b,   ANJAY_ID_RID,     0, "\xC7\x00",                 "1234567")
TLV_BYTES_TEST(len3b_id16b,  ANJAY_ID_RID, 42000, "\xE7\xA4\x10",             "1234567")

TLV_BYTES_TEST(len8b_id8b,   ANJAY_ID_RID,   255, "\xC8\xFF\x08",             "12345678")
TLV_BYTES_TEST(len8b_id16b,  ANJAY_ID_RID, 65534, "\xE8\xFF\xFE\x08",         "12345678")

TLV_BYTES_TEST(len16b_id8b,  ANJAY_ID_RID,    42, "\xD0\x2A\x03\xE8",         DATA1kB)
TLV_BYTES_TEST(len16b_id16b, ANJAY_ID_RID, 42420, "\xF0\xA5\xB4\x03\xE8",     DATA1kB)

TLV_BYTES_TEST(len24b_id8b,  ANJAY_ID_RID,    69, "\xD8\x45\x01\x86\xA0",     DATA100kB)
TLV_BYTES_TEST(len24b_id16b, ANJAY_ID_RID,   258, "\xF8\x01\x02\x01\x86\xA0", DATA100kB)

#undef TLV_BYTES_TEST
#undef TLV_BYTES_TEST_DATA

AVS_UNIT_TEST(tlv_in_bytes, id_too_short) {
    TEST_ENV(64);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, "\xE7\x00", 1));

    char buf[64];
    size_t bytes_read;
    bool message_finished;
    AVS_UNIT_ASSERT_FAILED(anjay_get_bytes(in, &bytes_read, &message_finished,
                                           buf, sizeof(buf)));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_bytes, length_too_short) {
    TEST_ENV(64);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream,
                                             "\xF8\x01\x02\x01\x86", 5));

    char buf[64];
    size_t bytes_read;
    bool message_finished;
    AVS_UNIT_ASSERT_FAILED(anjay_get_bytes(in, &bytes_read, &message_finished,
                                           buf, sizeof(buf)));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_bytes, partial_read) {
    TEST_ENV(16);
    static const char DATA[] = "\xC7\x2A" "0123456";
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, DATA, sizeof(DATA) - 1));

    for (size_t i = 0; i < 7; ++i) {
        char ch;
        size_t bytes_read;
        bool message_finished;
        AVS_UNIT_ASSERT_SUCCESS(
                anjay_get_bytes(in, &bytes_read, &message_finished, &ch, 1));
        if (i == 6) {
            AVS_UNIT_ASSERT_TRUE(message_finished);
        } else {
            AVS_UNIT_ASSERT_FALSE(message_finished);
            TLV_BYTES_TEST_ID(ANJAY_ID_RID, 42);
        }
        AVS_UNIT_ASSERT_EQUAL(bytes_read, 1);
        AVS_UNIT_ASSERT_EQUAL(message_finished, (i == 6));
        AVS_UNIT_ASSERT_EQUAL(ch, DATA[i + 2]);
    }

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_bytes, short_read_get_id) {
    TEST_ENV(64);
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write_f(stream, "%s", "\xC4\x2A" "0123"));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write_f(stream, "%s", "\xC7\x45" "0123456"));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write_f(stream, "%s", "\xC5\x16" "01234"));

    TLV_BYTES_TEST_ID(ANJAY_ID_RID, 42);
    TLV_BYTES_TEST_ID(ANJAY_ID_RID, 42);
    // skip reading altogether
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(in));

    TLV_BYTES_TEST_ID(ANJAY_ID_RID, 69);
    // short read
    char buf[3];
    size_t bytes_read;
    bool message_finished;
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_bytes(in, &bytes_read, &message_finished,
                                            buf, sizeof(buf)));
    AVS_UNIT_ASSERT_EQUAL(bytes_read, 3);
    AVS_UNIT_ASSERT_FALSE(message_finished);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buf, "012", 3);
    TLV_BYTES_TEST_ID(ANJAY_ID_RID, 69);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(in));

    TLV_BYTES_TEST_ID(ANJAY_ID_RID, 22);
    TLV_BYTES_TEST_ID(ANJAY_ID_RID, 22);
    // skip reading again
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(in));

    AVS_UNIT_ASSERT_EQUAL(_anjay_input_get_id(in, NULL, NULL),
                          ANJAY_GET_INDEX_END);
    TEST_TEARDOWN;
}

#undef TLV_BYTES_TEST_ID

AVS_UNIT_TEST(tlv_in_bytes, premature_end) {
    TEST_ENV(16);
    static const char DATA[] = "\xC7\x2A" "012";
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, DATA, sizeof(DATA) - 1));

    char buf[16];
    size_t bytes_read;
    bool message_finished;
    AVS_UNIT_ASSERT_FAILED(
            anjay_get_bytes(in, &bytes_read, &message_finished,
                            buf, sizeof(buf)));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_bytes, no_data) {
    TEST_ENV(16);

    char buf[16];
    size_t bytes_read;
    bool message_finished;
    AVS_UNIT_ASSERT_FAILED(
            anjay_get_bytes(in, &bytes_read, &message_finished,
                            buf, sizeof(buf)));

    TEST_TEARDOWN;
}

#undef TEST_TEARDOWN
#undef TEST_ENV

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    size_t bytes_left;
    const char *ptr;
} fake_tlv_in_t;

#define TEST_ENV(Data) \
    avs_stream_abstract_t *test_stream = NULL; \
    AVS_UNIT_ASSERT_SUCCESS(avs_unit_memstream_alloc(&test_stream, \
                                                     sizeof(Data))); \
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(test_stream, \
                                             Data, sizeof(Data) - 1)); \
    anjay_input_ctx_t *in = (anjay_input_ctx_t *) \
            &(tlv_in_t []) { { \
                .vtable = &TLV_IN_VTABLE, \
                .stream = { \
                    .vtable = &TLV_SINGLE_MSG_STREAM_WRAPPER_VTABLE, \
                    .backend = test_stream \
                }, \
                .id = 0, \
                .length = sizeof(Data) - 1 \
            } };

#define TEST_TEARDOWN avs_stream_cleanup(&test_stream)

AVS_UNIT_TEST(tlv_in_types, string_ok) {
    static const char TEST_STRING[] = "Hello, world!";
    TEST_ENV(TEST_STRING);

    char buf[16];
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_string(in, buf, sizeof(buf)));
    AVS_UNIT_ASSERT_EQUAL_STRING(buf, TEST_STRING);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_types, string_overflow) {
    static const char TEST_STRING[] = "Hello, world!";
    TEST_ENV(TEST_STRING);

    char buf[4];
    AVS_UNIT_ASSERT_FAILED(anjay_get_string(in, buf, sizeof(buf)));

    TEST_TEARDOWN;
}

#define TEST_NUM_IMPL(Name, Type, Suffix, Num, Data) \
AVS_UNIT_TEST(tlv_in_types, Name) { \
    TEST_ENV(Data); \
    Type value; \
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_##Suffix (in, &value)); \
    AVS_UNIT_ASSERT_EQUAL(value, (Type) Num); \
    TEST_TEARDOWN; \
}

#define TEST_NUM_FAIL_IMPL(Name, Type, Suffix, Data) \
AVS_UNIT_TEST(tlv_in_types, Name) { \
    TEST_ENV(Data); \
    Type value; \
    AVS_UNIT_ASSERT_FAILED(anjay_get_##Suffix (in, &value)); \
    TEST_TEARDOWN; \
}

#define TEST_NUM(Type, Suffix, Num, Data) \
        TEST_NUM_IMPL(AVS_CONCAT(Suffix##_, __LINE__), Type, Suffix, Num, Data)
#define TEST_NUM_FAIL(Type, Suffix, Data) \
        TEST_NUM_FAIL_IMPL(AVS_CONCAT(Suffix##fail_, __LINE__), Type, Suffix, Data)

#define TEST_INT32(...) TEST_NUM(int32_t, i32, __VA_ARGS__)
#define TEST_INT64(Num, Data) TEST_NUM(int64_t, i64, Num ## LL, Data)
#define TEST_INT3264(...) \
    TEST_INT32(__VA_ARGS__) \
    TEST_INT64(__VA_ARGS__)

#define TEST_INT3264_FAIL(...) \
    TEST_NUM_FAIL(int32_t, i32, __VA_ARGS__) \
    TEST_NUM_FAIL(int64_t, i64, __VA_ARGS__)

#define TEST_INT64ONLY(Num, Data) \
    TEST_NUM_FAIL(int32_t, i32, Data) \
    TEST_INT64(Num, Data)

TEST_INT3264_FAIL(                                                 "")
TEST_INT3264(                  42,                             "\x2A")
TEST_INT3264(                4242,                         "\x10\x92")
TEST_INT3264_FAIL(                                     "\x06\x79\x32")
TEST_INT3264(              424242,                 "\x00\x06\x79\x32")
TEST_INT3264(            42424242,                 "\x02\x87\x57\xB2")
TEST_INT3264((int32_t) 4242424242,                 "\xFC\xDE\x41\xB2")
TEST_INT64ONLY(        4242424242, "\x00\x00\x00\x00\xFC\xDE\x41\xB2")
TEST_INT3264_FAIL(                             "\x62\xC6\xD1\xA9\xB2")
TEST_INT64ONLY(      424242424242, "\x00\x00\x00\x62\xC6\xD1\xA9\xB2")
TEST_INT3264_FAIL(                         "\x26\x95\xA9\xE6\x49\xB2")
TEST_INT64ONLY(    42424242424242, "\x00\x00\x26\x95\xA9\xE6\x49\xB2")
TEST_INT3264_FAIL(                     "\x0F\x12\x76\x5D\xF4\xC9\xB2")
TEST_INT64ONLY(  4242424242424242, "\x00\x0F\x12\x76\x5D\xF4\xC9\xB2")
TEST_INT64ONLY(424242424242424242, "\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")
TEST_INT3264_FAIL(             "\x00\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")

#define TEST_FLOAT(Num, Data) \
        TEST_NUM(float, float, Num, Data) \
        TEST_NUM(double, double, Num, Data)

#define TEST_FLOAT_FAIL(Data) \
        TEST_NUM_FAIL(float, float, Data) \
        TEST_NUM_FAIL(double, double, Data)

TEST_FLOAT_FAIL(    "")
TEST_FLOAT_FAIL(    "\x3F")
TEST_FLOAT_FAIL(    "\x3F\x80")
TEST_FLOAT_FAIL(    "\x3F\x80\x00")
TEST_FLOAT(  1.0,   "\x3F\x80\x00\x00")
TEST_FLOAT(-42.0e3, "\xC7\x24\x10\x00")
TEST_FLOAT_FAIL(    "\x3F\xF0\x00\x00\x00")
TEST_FLOAT_FAIL(    "\x3F\xF0\x00\x00\x00\x00")
TEST_FLOAT_FAIL(    "\x3F\xF0\x00\x00\x00\x00\x00")
TEST_FLOAT(  1.0,   "\x3F\xF0\x00\x00\x00\x00\x00\x00")
TEST_FLOAT(  1.1,   "\x3F\xF1\x99\x99\x99\x99\x99\x9A")
TEST_FLOAT(-42.0e3, "\xC0\xE4\x82\x00\x00\x00\x00\x00")
TEST_FLOAT_FAIL(    "\xC0\xE4\x82\x00\x00\x00\x00\x00\x00")

#define TEST_BOOL_IMPL(Name, Value, Data) \
AVS_UNIT_TEST(tlv_in_types, Name) { \
    TEST_ENV(Data); \
    bool value; \
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_bool(in, &value)); \
    AVS_UNIT_ASSERT_EQUAL(!!(Value), value); \
    TEST_TEARDOWN; \
}

#define TEST_BOOL_FAIL_IMPL(Name, Data) \
AVS_UNIT_TEST(tlv_in_types, Name) { \
    TEST_ENV(Data); \
    bool value; \
    AVS_UNIT_ASSERT_FAILED(anjay_get_bool(in, &value)); \
    TEST_TEARDOWN; \
}

#define TEST_BOOL(Value, Data) \
        TEST_BOOL_IMPL(AVS_CONCAT(bool_, __LINE__), Value, Data)
#define TEST_BOOL_FAIL(Data) \
        TEST_BOOL_FAIL_IMPL(AVS_CONCAT(bool_, __LINE__), Data)

TEST_BOOL_FAIL(    "")
TEST_BOOL(false, "\0")
TEST_BOOL(true,  "\1")
TEST_BOOL_FAIL(  "\2")
TEST_BOOL_FAIL("\0\0")

#define TEST_OBJLNK_IMPL(Name, Oid, Iid, Data) \
AVS_UNIT_TEST(tlv_in_types, Name) { \
    TEST_ENV(Data); \
    anjay_oid_t oid; \
    anjay_iid_t iid; \
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_objlnk(in, &oid, &iid)); \
    AVS_UNIT_ASSERT_EQUAL(oid, Oid); \
    AVS_UNIT_ASSERT_EQUAL(iid, Iid); \
    TEST_TEARDOWN; \
}

#define TEST_OBJLNK_FAIL_IMPL(Name, Data) \
AVS_UNIT_TEST(tlv_in_types, Name) { \
    TEST_ENV(Data); \
    anjay_oid_t oid; \
    anjay_iid_t iid; \
    AVS_UNIT_ASSERT_FAILED(anjay_get_objlnk(in, &oid, &iid)); \
    TEST_TEARDOWN; \
}

#define TEST_OBJLNK(...) \
        TEST_OBJLNK_IMPL(AVS_CONCAT(objlnk_, __LINE__), __VA_ARGS__)
#define TEST_OBJLNK_FAIL(...) \
        TEST_OBJLNK_FAIL_IMPL(AVS_CONCAT(objlnk_, __LINE__), __VA_ARGS__)

TEST_OBJLNK_FAIL(         "")
TEST_OBJLNK_FAIL(         "\x00")
TEST_OBJLNK_FAIL(         "\x00\x00")
TEST_OBJLNK_FAIL(         "\x00\x00\x00")
TEST_OBJLNK(    0,     0, "\x00\x00\x00\x00")
TEST_OBJLNK(    1,     0, "\x00\x01\x00\x00")
TEST_OBJLNK(    0,     1, "\x00\x00\x00\x01")
TEST_OBJLNK(    1, 65535, "\x00\x01\xFF\xFF")
TEST_OBJLNK(65535,     1, "\xFF\xFF\x00\x01")
TEST_OBJLNK(65535, 65535, "\xFF\xFF\xFF\xFF")
TEST_OBJLNK_FAIL(         "\xFF\xFF\xFF\xFF\xFF")

AVS_UNIT_TEST(tlv_in_types, invalid_read) {
    TEST_ENV("\xC3\x00\x00\x00\x2A"); // bytes that contain an int afterward

    size_t bytes_read;
    bool message_finished;
    char ch;
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_bytes(in, &bytes_read, &message_finished,
                                            &ch, 1));

    int32_t value;
    AVS_UNIT_ASSERT_FAILED(anjay_get_i32(in, &value));

    TEST_TEARDOWN;
}
