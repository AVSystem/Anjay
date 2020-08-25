/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <anjay_init.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_unit_memstream.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#include "bigdata.h"
#include <anjay/core.h>

#define TEST_ENV(Size, Path)                            \
    avs_stream_t *stream = NULL;                        \
    ASSERT_OK(avs_unit_memstream_alloc(&stream, Size)); \
    anjay_input_ctx_t *in;                              \
    ASSERT_OK(_anjay_input_tlv_create(&in, &stream, &(Path)));

#define TEST_TEARDOWN                             \
    do {                                          \
        ASSERT_OK(_anjay_input_ctx_destroy(&in)); \
        ASSERT_OK(avs_stream_cleanup(&stream));   \
    } while (0)

#define TLV_BYTES_TEST_DATA(Header, Data)                                  \
    do {                                                                   \
        char *buf = (char *) avs_malloc(sizeof(Data) + sizeof(Header));    \
        size_t bytes_read;                                                 \
        bool message_finished;                                             \
        ASSERT_OK(anjay_get_bytes(in, &bytes_read, &message_finished, buf, \
                                  sizeof(Data) + sizeof(Header)));         \
        ASSERT_EQ(bytes_read, sizeof(Data) - 1);                           \
        ASSERT_TRUE(message_finished);                                     \
        ASSERT_EQ_BYTES(buf, Data);                                        \
        avs_free(buf);                                                     \
    } while (0)

static const anjay_uri_path_t TEST_INSTANCE_PATH =
        INSTANCE_PATH_INITIALIZER(3, 4);

#define MAKE_TEST_RESOURCE_PATH(Rid)                          \
    (MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID], \
                        TEST_INSTANCE_PATH.ids[ANJAY_ID_IID], (Rid)))

#define TLV_BYTES_TEST_PATH(Path)                           \
    do {                                                    \
        anjay_uri_path_t path;                              \
        ASSERT_OK(_anjay_input_get_path(in, &path, NULL));  \
        ASSERT_TRUE(_anjay_uri_path_equal(&path, &(Path))); \
    } while (0)

#define TLV_BYTES_TEST(Name, Path, Header, Data)                     \
    AVS_UNIT_TEST(tlv_in_bytes, Name##_with_id) {                    \
        TEST_ENV(sizeof(Data) + sizeof(Header), TEST_INSTANCE_PATH); \
        ASSERT_OK(avs_stream_write(stream, Header Data,              \
                                   sizeof(Header Data) - 1));        \
        TLV_BYTES_TEST_PATH(Path);                                   \
        TLV_BYTES_TEST_PATH(Path);                                   \
        TLV_BYTES_TEST_DATA(Header, Data);                           \
        TEST_TEARDOWN;                                               \
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
#undef TLV_BYTES_TEST_DATA

AVS_UNIT_TEST(tlv_in_bytes, id_too_short) {
    TEST_ENV(64, MAKE_ROOT_PATH());

    ASSERT_OK(avs_stream_write(stream, "\xE7\x00", 1));

    char buf[64];
    size_t bytes_read;
    bool message_finished;
    ASSERT_FAIL(anjay_get_bytes(in, &bytes_read, &message_finished, buf,
                                sizeof(buf)));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_bytes, length_too_short) {
    TEST_ENV(64, MAKE_ROOT_PATH());

    ASSERT_OK(avs_stream_write(stream, "\xF8\x01\x02\x01\x86", 5));

    char buf[64];
    size_t bytes_read;
    bool message_finished;
    ASSERT_FAIL(anjay_get_bytes(in, &bytes_read, &message_finished, buf,
                                sizeof(buf)));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_bytes, partial_read) {
    TEST_ENV(16, MAKE_INSTANCE_PATH(3, 4));
    static const char DATA[] = "\xC7\x2A"
                               "0123456";
    ASSERT_OK(avs_stream_write(stream, DATA, sizeof(DATA) - 1));

    for (size_t i = 0; i < 7; ++i) {
        char ch;
        size_t bytes_read;
        bool message_finished;
        ASSERT_OK(anjay_get_bytes(in, &bytes_read, &message_finished, &ch, 1));
        if (i == 6) {
            ASSERT_TRUE(message_finished);
        } else {
            ASSERT_FALSE(message_finished);
            TLV_BYTES_TEST_PATH(MAKE_RESOURCE_PATH(3, 4, 42));
        }
        ASSERT_EQ(bytes_read, 1);
        ASSERT_EQ(message_finished, (i == 6));
        ASSERT_EQ(ch, DATA[i + 2]);
    }

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_bytes, short_read_get_id) {
    TEST_ENV(64, MAKE_INSTANCE_PATH(3, 4));
    ASSERT_OK(avs_stream_write_f(stream, "%s",
                                 "\xC4\x2A"
                                 "0123"));
    ASSERT_OK(avs_stream_write_f(stream, "%s",
                                 "\xC7\x45"
                                 "0123456"));
    ASSERT_OK(avs_stream_write_f(stream, "%s",
                                 "\xC5\x16"
                                 "01234"));

    TLV_BYTES_TEST_PATH(MAKE_RESOURCE_PATH(3, 4, 42));
    TLV_BYTES_TEST_PATH(MAKE_RESOURCE_PATH(3, 4, 42));
    // skip reading altogether
    ASSERT_OK(_anjay_input_next_entry(in));

    TLV_BYTES_TEST_PATH(MAKE_RESOURCE_PATH(3, 4, 69));
    // short read
    char buf[3];
    size_t bytes_read;
    bool message_finished;
    ASSERT_OK(anjay_get_bytes(in, &bytes_read, &message_finished, buf,
                              sizeof(buf)));
    ASSERT_EQ(bytes_read, 3);
    ASSERT_FALSE(message_finished);
    ASSERT_EQ_BYTES_SIZED(buf, "012", 3);
    TLV_BYTES_TEST_PATH(MAKE_RESOURCE_PATH(3, 4, 69));
    ASSERT_OK(_anjay_input_next_entry(in));

    TLV_BYTES_TEST_PATH(MAKE_RESOURCE_PATH(3, 4, 22));
    TLV_BYTES_TEST_PATH(MAKE_RESOURCE_PATH(3, 4, 22));
    // skip reading again
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_EQ(_anjay_input_get_path(in, &(anjay_uri_path_t) { 0 }, NULL),
              ANJAY_GET_PATH_END);
    TEST_TEARDOWN;
}

#undef TLV_BYTES_TEST_PATH

AVS_UNIT_TEST(tlv_in_bytes, premature_end) {
    TEST_ENV(16, MAKE_ROOT_PATH());
    static const char DATA[] = "\xC7\x2A"
                               "012";
    ASSERT_OK(avs_stream_write(stream, DATA, sizeof(DATA) - 1));

    char buf[16];
    size_t bytes_read;
    bool message_finished;
    ASSERT_FAIL(anjay_get_bytes(in, &bytes_read, &message_finished, buf,
                                sizeof(buf)));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_bytes, no_data) {
    TEST_ENV(16, MAKE_ROOT_PATH());

    unsigned seed = (unsigned) avs_time_real_now().since_real_epoch.seconds;
    const char init = (char) avs_rand_r(&seed);
    char buf[16] = { init };
    size_t bytes_read;
    bool message_finished;
    ASSERT_OK(anjay_get_bytes(in, &bytes_read, &message_finished, buf,
                              sizeof(buf)));
    /* buffer untouched, read 0 bytes */
    ASSERT_EQ(buf[0], init);

    TEST_TEARDOWN;
}

#undef TEST_TEARDOWN
#undef TEST_ENV

#define TEST_ENV(Data)                                                   \
    avs_stream_t *stream = NULL;                                         \
    ASSERT_OK(avs_unit_memstream_alloc(&stream, sizeof(Data)));          \
    ASSERT_OK(avs_stream_write(stream, Data, sizeof(Data) - 1));         \
    anjay_input_ctx_t *in;                                               \
    ASSERT_OK(_anjay_input_tlv_create(&in, &stream, &MAKE_ROOT_PATH())); \
    tlv_in_t *ctx = (tlv_in_t *) in;                                     \
    ctx->has_path = true;                                                \
    tlv_entry_t *entry = tlv_entry_push(ctx);                            \
    AVS_UNIT_ASSERT_NOT_NULL(entry);                                     \
    entry->length = sizeof(Data) - 1;

#define TEST_TEARDOWN                             \
    do {                                          \
        ASSERT_OK(_anjay_input_ctx_destroy(&in)); \
        ASSERT_OK(avs_stream_cleanup(&stream));   \
    } while (0)

AVS_UNIT_TEST(tlv_in_types, string_ok) {
    static const char TEST_STRING[] = "Hello, world!";
    TEST_ENV(TEST_STRING);

    char buf[16];
    ASSERT_OK(anjay_get_string(in, buf, sizeof(buf)));
    ASSERT_EQ_STR(buf, TEST_STRING);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_types, string_overflow) {
    static const char TEST_STRING[] = "Hello, world!";
    TEST_ENV(TEST_STRING);

    char buf[4];
    ASSERT_EQ(anjay_get_string(in, buf, sizeof(buf)), ANJAY_BUFFER_TOO_SHORT);
    ASSERT_EQ_STR(buf, "Hel");
    ASSERT_EQ(anjay_get_string(in, buf, sizeof(buf)), ANJAY_BUFFER_TOO_SHORT);
    ASSERT_EQ_STR(buf, "lo,");
    ASSERT_EQ(anjay_get_string(in, buf, sizeof(buf)), ANJAY_BUFFER_TOO_SHORT);
    ASSERT_EQ_STR(buf, " wo");
    ASSERT_EQ(anjay_get_string(in, buf, sizeof(buf)), ANJAY_BUFFER_TOO_SHORT);
    ASSERT_EQ_STR(buf, "rld");
    ASSERT_OK(anjay_get_string(in, buf, sizeof(buf)));
    ASSERT_EQ_STR(buf, "!");

    TEST_TEARDOWN;
}

#define TEST_NUM_IMPL(Name, Type, Suffix, Num, Data) \
    AVS_UNIT_TEST(tlv_in_types, Name) {              \
        TEST_ENV(Data);                              \
        Type value;                                  \
        ASSERT_OK(anjay_get_##Suffix(in, &value));   \
        ASSERT_EQ(value, (Type) Num);                \
        TEST_TEARDOWN;                               \
    }

#define TEST_NUM_FAIL_IMPL(Name, Type, Suffix, Data) \
    AVS_UNIT_TEST(tlv_in_types, Name) {              \
        TEST_ENV(Data);                              \
        Type value;                                  \
        ASSERT_FAIL(anjay_get_##Suffix(in, &value)); \
        TEST_TEARDOWN;                               \
    }

#define TEST_NUM(Type, Suffix, Num, Data) \
    TEST_NUM_IMPL(AVS_CONCAT(Suffix##_, __LINE__), Type, Suffix, Num, Data)
#define TEST_NUM_FAIL(Type, Suffix, Data) \
    TEST_NUM_FAIL_IMPL(AVS_CONCAT(Suffix##fail_, __LINE__), Type, Suffix, Data)

#define TEST_INT32(...) TEST_NUM(int32_t, i32, __VA_ARGS__)
#define TEST_INT64(Num, Data) TEST_NUM(int64_t, i64, Num##LL, Data)
#define TEST_INT3264(...)   \
    TEST_INT32(__VA_ARGS__) \
    TEST_INT64(__VA_ARGS__)

#define TEST_INT3264_FAIL(...)               \
    TEST_NUM_FAIL(int32_t, i32, __VA_ARGS__) \
    TEST_NUM_FAIL(int64_t, i64, __VA_ARGS__)

#define TEST_INT64ONLY(Num, Data)     \
    TEST_NUM_FAIL(int32_t, i32, Data) \
    TEST_INT64(Num, Data)

TEST_INT3264_FAIL("")
TEST_INT3264(42, "\x2A")
TEST_INT3264(4242, "\x10\x92")
TEST_INT3264_FAIL("\x06\x79\x32")
TEST_INT3264(424242, "\x00\x06\x79\x32")
TEST_INT3264(42424242, "\x02\x87\x57\xB2")
TEST_INT3264((int32_t) 4242424242, "\xFC\xDE\x41\xB2")
TEST_INT64ONLY(4242424242, "\x00\x00\x00\x00\xFC\xDE\x41\xB2")
TEST_INT3264_FAIL("\x62\xC6\xD1\xA9\xB2")
TEST_INT64ONLY(424242424242, "\x00\x00\x00\x62\xC6\xD1\xA9\xB2")
TEST_INT3264_FAIL("\x26\x95\xA9\xE6\x49\xB2")
TEST_INT64ONLY(42424242424242, "\x00\x00\x26\x95\xA9\xE6\x49\xB2")
TEST_INT3264_FAIL("\x0F\x12\x76\x5D\xF4\xC9\xB2")
TEST_INT64ONLY(4242424242424242, "\x00\x0F\x12\x76\x5D\xF4\xC9\xB2")
TEST_INT64ONLY(424242424242424242, "\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")
TEST_INT3264_FAIL("\x00\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")

#define TEST_UINT32(...) TEST_NUM(uint32_t, u32, __VA_ARGS__)
#define TEST_UINT64(Num, Data) TEST_NUM(uint64_t, u64, Num##ULL, Data)
#define TEST_UINT3264(...)   \
    TEST_UINT32(__VA_ARGS__) \
    TEST_UINT64(__VA_ARGS__)

#define TEST_FLOAT(Num, Data)         \
    TEST_NUM(float, float, Num, Data) \
    TEST_NUM(double, double, Num, Data)

#define TEST_FLOAT_FAIL(Data)         \
    TEST_NUM_FAIL(float, float, Data) \
    TEST_NUM_FAIL(double, double, Data)

TEST_FLOAT_FAIL("")
TEST_FLOAT_FAIL("\x3F")
TEST_FLOAT_FAIL("\x3F\x80")
TEST_FLOAT_FAIL("\x3F\x80\x00")
TEST_FLOAT(1.0, "\x3F\x80\x00\x00")
TEST_FLOAT(-42.0e3, "\xC7\x24\x10\x00")
TEST_FLOAT_FAIL("\x3F\xF0\x00\x00\x00")
TEST_FLOAT_FAIL("\x3F\xF0\x00\x00\x00\x00")
TEST_FLOAT_FAIL("\x3F\xF0\x00\x00\x00\x00\x00")
TEST_FLOAT(1.0, "\x3F\xF0\x00\x00\x00\x00\x00\x00")
TEST_FLOAT(1.1, "\x3F\xF1\x99\x99\x99\x99\x99\x9A")
TEST_FLOAT(-42.0e3, "\xC0\xE4\x82\x00\x00\x00\x00\x00")
TEST_FLOAT_FAIL("\xC0\xE4\x82\x00\x00\x00\x00\x00\x00")

#define TEST_BOOL_IMPL(Name, Value, Data)      \
    AVS_UNIT_TEST(tlv_in_types, Name) {        \
        TEST_ENV(Data);                        \
        bool value;                            \
        ASSERT_OK(anjay_get_bool(in, &value)); \
        ASSERT_EQ(!!(Value), value);           \
        TEST_TEARDOWN;                         \
    }

#define TEST_BOOL_FAIL_IMPL(Name, Data)          \
    AVS_UNIT_TEST(tlv_in_types, Name) {          \
        TEST_ENV(Data);                          \
        bool value;                              \
        ASSERT_FAIL(anjay_get_bool(in, &value)); \
        TEST_TEARDOWN;                           \
    }

#define TEST_BOOL(Value, Data) \
    TEST_BOOL_IMPL(AVS_CONCAT(bool_, __LINE__), Value, Data)
#define TEST_BOOL_FAIL(Data) \
    TEST_BOOL_FAIL_IMPL(AVS_CONCAT(bool_, __LINE__), Data)

TEST_BOOL_FAIL("")
TEST_BOOL(false, "\0")
TEST_BOOL(true, "\1")
TEST_BOOL_FAIL("\2")
TEST_BOOL_FAIL("\0\0")

#define TEST_OBJLNK_IMPL(Name, Oid, Iid, Data)       \
    AVS_UNIT_TEST(tlv_in_types, Name) {              \
        TEST_ENV(Data);                              \
        anjay_oid_t oid;                             \
        anjay_iid_t iid;                             \
        ASSERT_OK(anjay_get_objlnk(in, &oid, &iid)); \
        ASSERT_EQ(oid, Oid);                         \
        ASSERT_EQ(iid, Iid);                         \
        TEST_TEARDOWN;                               \
    }

#define TEST_OBJLNK_FAIL_IMPL(Name, Data)              \
    AVS_UNIT_TEST(tlv_in_types, Name) {                \
        TEST_ENV(Data);                                \
        anjay_oid_t oid;                               \
        anjay_iid_t iid;                               \
        ASSERT_FAIL(anjay_get_objlnk(in, &oid, &iid)); \
        TEST_TEARDOWN;                                 \
    }

#define TEST_OBJLNK(...) \
    TEST_OBJLNK_IMPL(AVS_CONCAT(objlnk_, __LINE__), __VA_ARGS__)
#define TEST_OBJLNK_FAIL(...) \
    TEST_OBJLNK_FAIL_IMPL(AVS_CONCAT(objlnk_, __LINE__), __VA_ARGS__)

TEST_OBJLNK_FAIL("")
TEST_OBJLNK_FAIL("\x00")
TEST_OBJLNK_FAIL("\x00\x00")
TEST_OBJLNK_FAIL("\x00\x00\x00")
TEST_OBJLNK(0, 0, "\x00\x00\x00\x00")
TEST_OBJLNK(1, 0, "\x00\x01\x00\x00")
TEST_OBJLNK(0, 1, "\x00\x00\x00\x01")
TEST_OBJLNK(1, 65535, "\x00\x01\xFF\xFF")
TEST_OBJLNK(65535, 1, "\xFF\xFF\x00\x01")
TEST_OBJLNK(65535, 65535, "\xFF\xFF\xFF\xFF")
TEST_OBJLNK_FAIL("\xFF\xFF\xFF\xFF\xFF")

AVS_UNIT_TEST(tlv_in_types, invalid_read) {
    TEST_ENV("\xC3\x00\x00\x00\x2A"); // bytes that contain an int afterward

    size_t bytes_read;
    bool message_finished;
    char ch;
    ASSERT_OK(anjay_get_bytes(in, &bytes_read, &message_finished, &ch, 1));

    int32_t value;
    ASSERT_FAIL(anjay_get_i32(in, &value));

    TEST_TEARDOWN;
}

#undef TEST_ENV

#define TEST_ENV(Data, Path)                                     \
    avs_stream_t *stream = NULL;                                 \
    ASSERT_OK(avs_unit_memstream_alloc(&stream, sizeof(Data)));  \
    ASSERT_OK(avs_stream_write(stream, Data, sizeof(Data) - 1)); \
    anjay_input_ctx_t *in;                                       \
    ASSERT_OK(_anjay_input_tlv_create(&in, &stream, &(Path)));

AVS_UNIT_TEST(tlv_in_path, typical_payload_for_create_without_iid) {
    TEST_ENV("\xC7\x00"
             "1234567",
             MAKE_OBJECT_PATH(42));

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path, &MAKE_RESOURCE_PATH(42, ANJAY_ID_INVALID, 0)));
    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_path, payload_write_on_instance_with_rids_only) {
    // [ RID(1)=10, RID(2)=10, RID(3)=10 ]
    TEST_ENV("\xc1\x01\x0a\xc1\x02\x0a\xc1\x03\x0a", MAKE_INSTANCE_PATH(3, 4));
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(3, 4, 2)));
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(3, 4, 3)));
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_EQ(_anjay_input_get_path(in, &path, NULL), ANJAY_GET_PATH_END);
    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_path,
              payload_write_on_instance_with_rids_uri_iid_mismatch) {
    // IID(5, [ RID(1)=10 ])
    TEST_ENV("\x03\x05\xc1\x01\x0a", MAKE_INSTANCE_PATH(3, 4));
    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_path, fail_on_path_with_invalid_iid) {
    // IID(ANJAY_ID_INVALID, [ RID(1)=1 ])
    TEST_ENV("\x23\xff\xff\xc1\x01\x0a", MAKE_ROOT_PATH());
    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_path, fail_on_path_with_invalid_rid) {
    // IID(5, [ RID(1)=ANJAY_ID_INVALID ])
    TEST_ENV("\x04\x05\xe1\xff\xff\x0a", MAKE_ROOT_PATH());
    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_path, fail_on_path_with_invalid_riid) {
    // RIID=ANJAY_ID_INVALID
    TEST_ENV("\x61\xff\xff\x0a", MAKE_RESOURCE_PATH(5, 0, 1));
    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_path, payload_write_on_instance_with_rids) {
    // IID(4, [ RID(1)=10, RID(2)=10 ])
    TEST_ENV("\x06\x04\xc1\x01\x0a\xc1\x02\x0a", MAKE_INSTANCE_PATH(3, 4));
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(3, 4, 1)));
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(3, 4, 2)));
    ASSERT_OK(_anjay_input_next_entry(in));

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_path, payload_write_on_resource_with_riids_only) {
    TEST_ENV("\x41\x01\x0a\x41\x02\x0a\x41\x03\x0a",
             MAKE_RESOURCE_PATH(3, 4, 5));
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path, &MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 1)));
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path, &MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 2)));
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path, &MAKE_RESOURCE_INSTANCE_PATH(3, 4, 5, 3)));
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_EQ(_anjay_input_get_path(in, &path, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_array, tlv_id_is_array) {
    TEST_ENV("\x80\x05", MAKE_RESOURCE_PATH(3, 4, 5));
    anjay_uri_path_t path;
    bool is_array;
    ASSERT_OK(_anjay_input_get_path(in, &path, &is_array));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(3, 4, 5)));
    ASSERT_EQ(is_array, true);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(tlv_in_empty, empty_instances_list) {
    // [ Instance(1), Instance(2) ]
    TEST_ENV("\x00\x01\x00\x02", MAKE_OBJECT_PATH(3));
    anjay_uri_path_t path;
    bool is_array;
    ASSERT_OK(_anjay_input_get_path(in, &path, &is_array));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_INSTANCE_PATH(3, 1)));
    ASSERT_EQ(is_array, false);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_INSTANCE_PATH(3, 2)));

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN;
}
