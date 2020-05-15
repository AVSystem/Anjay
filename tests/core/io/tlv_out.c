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
#include <avsystem/commons/avs_unit_test.h>

#include "bigdata.h"

///////////////////////////////////////////////////////////// ENCODING // SIMPLE

#define TEST_ENV_COMMON(Uri)                                           \
    avs_stream_outbuf_t outbuf = AVS_STREAM_OUTBUF_STATIC_INITIALIZER; \
    avs_stream_outbuf_set_buffer(&outbuf, buf, sizeof(buf));           \
    anjay_output_ctx_t *out =                                          \
            _anjay_output_tlv_create((avs_stream_t *) &outbuf, (Uri)); \
    AVS_UNIT_ASSERT_NOT_NULL(out)

#define TEST_ENV(Size, Uri) \
    char buf[Size];         \
    TEST_ENV_COMMON((Uri))

#define TEST_ENV_HEAP(Size, Uri)           \
    char *buf = (char *) avs_malloc(Size); \
    TEST_ENV_COMMON((Uri))

#define VERIFY_BYTES(Data)                                       \
    do {                                                         \
        AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&outbuf), \
                              sizeof(Data) - 1);                 \
        AVS_UNIT_ASSERT_EQUAL_BYTES(buf, Data);                  \
    } while (0)

// 3 bits for length - <=7
AVS_UNIT_TEST(tlv_out, bytes_3blen_8bid) {
    static const char DATA[] = "1234567";
    TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\xC7\x00"
                 "1234567");
}

AVS_UNIT_TEST(tlv_out, bytes_3blen_16bid) {
    // 3 bits for length - <=7
    static const char DATA[] = "1234567";
    TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42000)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\xE7\xA4\x10"
                 "1234567");
}

AVS_UNIT_TEST(tlv_out, bytes_8blen_8bid) {
    static const char DATA[] = "12345678";
    TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 255)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\xC8\xFF\x08"
                 "12345678");
}

AVS_UNIT_TEST(tlv_out, bytes_8blen_16bid) {
    static const char DATA[] = "12345678";
    TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 65534)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\xE8\xFF\xFE\x08"
                 "12345678");
}

AVS_UNIT_TEST(tlv_out, bytes_16blen_8bid) {
    TEST_ENV(1024, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA1kB));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\xD0\x2A\x03\xE8" DATA1kB);
}

AVS_UNIT_TEST(tlv_out, bytes_16blen_16bid) {
    TEST_ENV(1024, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42420)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA1kB));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\xF0\xA5\xB4\x03\xE8" DATA1kB);
}

AVS_UNIT_TEST(tlv_out, bytes_24blen_8bid) {
    TEST_ENV(102400, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 69)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA100kB));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\xD8\x45\x01\x86\xA0" DATA100kB);
}

AVS_UNIT_TEST(tlv_out, bytes_24blen_16bid) {
    TEST_ENV(102400, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 258)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA100kB));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\xF8\x01\x02\x01\x86\xA0" DATA100kB);
}

AVS_UNIT_TEST(tlv_out, bytes_overlength) {
    TEST_ENV_HEAP(20 * 1024 * 1024, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 1)));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_string(out, DATA20MB));
    AVS_UNIT_ASSERT_FAILED(_anjay_output_ctx_destroy(&out));
    avs_free(buf);
}

AVS_UNIT_TEST(tlv_out, zero_id) {
    TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, "test"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
}

#define TEST_INT_IMPL(Name, Bits, Num, Data)                                \
    AVS_UNIT_TEST(tlv_out, Name) {                                          \
        TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));                            \
                                                                            \
        AVS_UNIT_ASSERT_SUCCESS(                                            \
                _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 1))); \
        AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i##Bits(out, Num));               \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));           \
        VERIFY_BYTES(Data);                                                 \
    }

#define TEST_INT(Bits, Num, Data) \
    TEST_INT_IMPL(AVS_CONCAT(i##Bits##_, __LINE__), Bits, Num, Data)

#define TEST_INT32(...) TEST_INT(32, __VA_ARGS__)
#define TEST_INT64(Num, Data) TEST_INT(64, Num##LL, Data)
#define TEST_INT3264(...)   \
    TEST_INT32(__VA_ARGS__) \
    TEST_INT64(__VA_ARGS__)

TEST_INT3264(42,
             "\xC1\x01"
             "\x2A")
TEST_INT3264(4242,
             "\xC2\x01"
             "\x10\x92")
TEST_INT3264(424242,
             "\xC4\x01"
             "\x00\x06\x79\x32")
TEST_INT3264(42424242,
             "\xC4\x01"
             "\x02\x87\x57\xB2")
TEST_INT3264((int32_t) 4242424242,
             "\xC4\x01"
             "\xFC\xDE\x41\xB2")
TEST_INT64(4242424242, "\xC8\x01\x08\x00\x00\x00\x00\xFC\xDE\x41\xB2")
TEST_INT64(424242424242, "\xC8\x01\x08\x00\x00\x00\x62\xC6\xD1\xA9\xB2")
TEST_INT64(42424242424242, "\xC8\x01\x08\x00\x00\x26\x95\xA9\xE6\x49\xB2")
TEST_INT64(4242424242424242, "\xC8\x01\x08\x00\x0F\x12\x76\x5D\xF4\xC9\xB2")
TEST_INT64(424242424242424242, "\xC8\x01\x08\x05\xE3\x36\x3C\xB3\x9E\xC9\xB2")

#define TEST_FLOAT_IMPL(Name, Type, Num, Data)                              \
    AVS_UNIT_TEST(tlv_out, Name) {                                          \
        TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));                            \
                                                                            \
        AVS_UNIT_ASSERT_SUCCESS(                                            \
                _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 1))); \
        AVS_UNIT_ASSERT_SUCCESS(anjay_ret_##Type(out, Num));                \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));           \
        VERIFY_BYTES(Data);                                                 \
    }

#define TEST_FLOAT(Num, Data) \
    TEST_FLOAT_IMPL(AVS_CONCAT(float, __LINE__), float, Num, Data)

TEST_FLOAT(1.0, "\xC4\x01\x3F\x80\x00\x00")
TEST_FLOAT(-42.0e3, "\xC4\x01\xC7\x24\x10\x00")

#define TEST_DOUBLE(Num, Data) \
    TEST_FLOAT_IMPL(AVS_CONCAT(double, __LINE__), double, Num, Data)

// rounds exactly to float
TEST_DOUBLE(1.0, "\xC4\x01\x3F\x80\x00\x00")

// using double increases precision
TEST_DOUBLE(1.1, "\xC8\x01\x08\x3F\xF1\x99\x99\x99\x99\x99\x9A")

#define TEST_BOOL(Val, Data)                                                \
    AVS_UNIT_TEST(tlv_out, bool_##Val) {                                    \
        TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));                            \
                                                                            \
        AVS_UNIT_ASSERT_SUCCESS(                                            \
                _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 1))); \
        AVS_UNIT_ASSERT_SUCCESS(anjay_ret_bool(out, Val));                  \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));           \
        VERIFY_BYTES("\xC1\x01" Data);                                      \
    }

TEST_BOOL(true, "\1")
TEST_BOOL(false, "\0")
TEST_BOOL(1, "\1")
TEST_BOOL(0, "\0")
TEST_BOOL(42, "\1")

#define TEST_OBJLNK(Oid, Iid, Data)                                         \
    AVS_UNIT_TEST(tlv_out, objlnk_##Oid##_##Iid) {                          \
        TEST_ENV(32, &MAKE_INSTANCE_PATH(0, 0));                            \
                                                                            \
        AVS_UNIT_ASSERT_SUCCESS(                                            \
                _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 1))); \
        AVS_UNIT_ASSERT_SUCCESS(anjay_ret_objlnk(out, Oid, Iid));           \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));           \
        VERIFY_BYTES("\xC4\x01" Data);                                      \
    }

TEST_OBJLNK(0, 0, "\x00\x00\x00\x00")
TEST_OBJLNK(1, 0, "\x00\x01\x00\x00")
TEST_OBJLNK(0, 1, "\x00\x00\x00\x01")
TEST_OBJLNK(1, 65535, "\x00\x01\xFF\xFF")
TEST_OBJLNK(65535, 1, "\xFF\xFF\x00\x01")
TEST_OBJLNK(65535, 65535, "\xFF\xFF\xFF\xFF")

////////////////////////////////////////////////////////////// ENCODING // ARRAY

AVS_UNIT_TEST(tlv_out_array, simple) {
    TEST_ENV(512, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_path(
            out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 1, 42)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i32(out, 69));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_path(
            out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 1, 514)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i32(out, 696969));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 2)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i32(out, 4));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("\x88\x01\x0A"                 // array
                 "\x41\x2A\x45"                 // first entry
                 "\x64\x02\x02\x00\x0A\xA2\x89" // second entry
                 "\xC1\x02\x04"                 // another entry
    );
}

AVS_UNIT_TEST(tlv_out_array, too_long) {
    TEST_ENV_HEAP(100 * 1024 * 1024, &MAKE_INSTANCE_PATH(0, 0));

    for (size_t i = 0; i < 20; ++i) {
        // 1 MB each entry, 20 MB altogether
        AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_path(
                out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 1, 1)));
        AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA1MB));
    }
    AVS_UNIT_ASSERT_FAILED(_anjay_output_ctx_destroy(&out));
    avs_free(buf);
}

AVS_UNIT_TEST(tlv_out_array, array_index) {
    TEST_ENV(512, &MAKE_INSTANCE_PATH(0, 0));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_path(
            out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 1, 65534)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i32(out, 69));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
}

AVS_UNIT_TEST(tlv_out, object_with_empty_bytes) {
    TEST_ENV(512, &MAKE_OBJECT_PATH(0));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 1, 0)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_bytes(out, "", 0));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 1, 1)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_bytes(out, "", 1));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
}

//////////////////////////////////////////// ENCODING // ADDITIONAL CORNER CASES

AVS_UNIT_TEST(tlv_out, riid_as_root) {
    static const char DATA[] = "1234567";
    TEST_ENV(512, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_path(
            out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, DATA));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\x47\x00"
                 "1234567");
}

AVS_UNIT_TEST(tlv_out, set_path) {
    TEST_ENV(512, &MAKE_OBJECT_PATH(0));
    AVS_UNIT_ASSERT_EQUAL(((tlv_out_t *) out)->level, TLV_OUT_LEVEL_IID);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_IID].next_id,
            ANJAY_ID_INVALID);

    // set path downwards
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_path(
            out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0)));
    AVS_UNIT_ASSERT_EQUAL(((tlv_out_t *) out)->level, TLV_OUT_LEVEL_RIID);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_IID].next_id, 0);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_RID].next_id, 0);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_RIID].next_id, 0);
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_bytes(out, NULL, 0));

    // set path upwards
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_output_set_path(out, &MAKE_INSTANCE_PATH(0, 0)));
    AVS_UNIT_ASSERT_EQUAL(((tlv_out_t *) out)->level, TLV_OUT_LEVEL_IID);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_IID].next_id, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_start_aggregate(out));

    // set path downwards again
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_path(
            out, &MAKE_RESOURCE_INSTANCE_PATH(0, 1, 2, 3)));
    AVS_UNIT_ASSERT_EQUAL(((tlv_out_t *) out)->level, TLV_OUT_LEVEL_RIID);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_IID].next_id, 1);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_RID].next_id, 2);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_RIID].next_id, 3);
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_bytes(out, NULL, 0));

    // set unrelated path
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_path(
            out, &MAKE_RESOURCE_INSTANCE_PATH(0, 4, 5, 6)));
    AVS_UNIT_ASSERT_EQUAL(((tlv_out_t *) out)->level, TLV_OUT_LEVEL_RIID);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_IID].next_id, 4);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_RID].next_id, 5);
    AVS_UNIT_ASSERT_EQUAL(
            ((tlv_out_t *) out)->levels[TLV_OUT_LEVEL_RIID].next_id, 6);
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_bytes(out, NULL, 0));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));
    VERIFY_BYTES("\x04\x00" // instance /0/0
                 "\x82\x00" // multiple resource /0/0/0
                 "\x40\x00" // resource instance /0/0/0/0
                 "\x00\x00" // instance /0/0 (again)
                 "\x04\x01" // instance /0/1
                 "\x82\x02" // multiple resource /0/1/2
                 "\x40\x03" // resource instance /0/1/2/3
                 "\x04\x04" // instance /0/4
                 "\x82\x05" // multiple resource /0/4/5
                 "\x40\x06" // resource instance /0/4/5/6
    );
}
