/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_unit_memstream.h>
#include <avsystem/commons/avs_unit_test.h>

#include "tests/utils/utils.h"

/////////////////////////////////////////////////////////////////////// ENCODING

static void text_out_destroy(anjay_unlocked_ret_bytes_ctx_t ***ctx) {
    if (ctx && *ctx && **ctx) {
        _anjay_base64_ret_bytes_ctx_delete(*ctx);
    }
}

#define TEST_ENV(Size)                                                 \
    char buf[Size];                                                    \
    avs_stream_outbuf_t outbuf = AVS_STREAM_OUTBUF_STATIC_INITIALIZER; \
    text_out_t out = { { &TEXT_OUT_VTABLE, 0 },                        \
                       NULL,                                           \
                       (avs_stream_t *) &outbuf,                       \
                       STATE_PATH_SET };                               \
    SCOPED_PTR(anjay_unlocked_ret_bytes_ctx_t *, text_out_destroy)     \
    _ret_bytes = &out.bytes;                                           \
    (void) _ret_bytes;                                                 \
    avs_stream_outbuf_set_buffer(&outbuf, buf, sizeof(buf))

static void stringify_buf(avs_stream_outbuf_t *outbuf) {
    outbuf->message_finished = 0;
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write((avs_stream_t *) outbuf, "", 1));
}

AVS_UNIT_TEST(text_out, string) {
    TEST_ENV(512);
    static const char TEST_STRING[] = "Hello, world!";

    AVS_UNIT_ASSERT_SUCCESS(_anjay_ret_string_unlocked(
            (anjay_unlocked_output_ctx_t *) &out, TEST_STRING));
    stringify_buf(&outbuf);
    AVS_UNIT_ASSERT_EQUAL_STRING(buf, TEST_STRING);
}

AVS_UNIT_TEST(text_out, string_err) {
    TEST_ENV(8);
    static const char TEST_STRING[] = "Hello, world!";

    AVS_UNIT_ASSERT_FAILED(_anjay_ret_string_unlocked(
            (anjay_unlocked_output_ctx_t *) &out, TEST_STRING));
}

#define TEST_i64(Val)                                            \
    do {                                                         \
        TEST_ENV(512);                                           \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_ret_i64_unlocked(         \
                (anjay_unlocked_output_ctx_t *) &out, Val##LL)); \
        stringify_buf(&outbuf);                                  \
        AVS_UNIT_ASSERT_EQUAL_STRING(buf, #Val);                 \
    } while (false)

AVS_UNIT_TEST(text_out, i64) {
    TEST_i64(-1000000000000000000);
    TEST_i64(514);
    TEST_i64(0);
    TEST_i64(-1);
    TEST_i64(2147483647);
    TEST_i64(-2147483648);
    TEST_i64(1000000000000000000);
}

#undef TEST_i64

#define TEST_DOUBLE_IMPL(Val, Str)                           \
    do {                                                     \
        TEST_ENV(512);                                       \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_ret_double_unlocked(  \
                (anjay_unlocked_output_ctx_t *) &out, Val)); \
        stringify_buf(&outbuf);                              \
        AVS_UNIT_ASSERT_EQUAL_STRING(buf, Str);              \
    } while (false)

#define TEST_DOUBLE(Val) TEST_DOUBLE_IMPL(Val, #Val)

AVS_UNIT_TEST(text_out, f64) {
    TEST_DOUBLE(0);
    TEST_DOUBLE(1);
    TEST_DOUBLE(1.2);
    TEST_DOUBLE(1.3125);
#ifdef AVS_COMMONS_WITHOUT_FLOAT_FORMAT_SPECIFIERS
    // NOTE: This variant of AVS_DOUBLE_AS_STRING() is slightly inaccurate in
    // order to keep the implementation simpler. This level of inaccuracy is
    // unlikely to cause problems in real-world applications.
    TEST_DOUBLE_IMPL(4.2229999965160742e+37, "4.2229999965160736e+37");
#else  // AVS_COMMONS_WITHOUT_FLOAT_FORMAT_SPECIFIERS
    TEST_DOUBLE(4.2229999965160742e+37);
#endif // AVS_COMMONS_WITHOUT_FLOAT_FORMAT_SPECIFIERS
    TEST_DOUBLE(10000.5);
    TEST_DOUBLE(10000000000000.5);
    TEST_DOUBLE(3.26e+218);
}

#undef TEST_DOUBLE

#define TEST_BOOL(Val)                                       \
    do {                                                     \
        TEST_ENV(512);                                       \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_ret_bool_unlocked(    \
                (anjay_unlocked_output_ctx_t *) &out, Val)); \
        stringify_buf(&outbuf);                              \
        AVS_UNIT_ASSERT_EQUAL_STRING(buf, Val ? "1" : "0");  \
    } while (false)

AVS_UNIT_TEST(text_out, boolean) {
    TEST_BOOL(true);
    TEST_BOOL(false);
    TEST_BOOL(1);
    TEST_BOOL(0);
    TEST_BOOL(42);
}

#undef TEST_BOOL

#define TEST_OBJLNK(Oid, Iid)                                     \
    do {                                                          \
        TEST_ENV(512);                                            \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_ret_objlnk_unlocked(       \
                (anjay_unlocked_output_ctx_t *) &out, Oid, Iid)); \
        stringify_buf(&outbuf);                                   \
        AVS_UNIT_ASSERT_EQUAL_STRING(buf, #Oid ":" #Iid);         \
    } while (false)

AVS_UNIT_TEST(text_out, objlnk) {
    TEST_OBJLNK(0, 0);
    TEST_OBJLNK(1, 0);
    TEST_OBJLNK(0, 1);
    TEST_OBJLNK(1, 65535);
    TEST_OBJLNK(65535, 1);
    TEST_OBJLNK(65535, 65535);
}

#undef TEST_OBJLNK

AVS_UNIT_TEST(text_out, unimplemented) {
    TEST_ENV(512);
    AVS_UNIT_ASSERT_NOT_NULL(_anjay_ret_bytes_begin_unlocked(
            (anjay_unlocked_output_ctx_t *) &out, 3));
    AVS_UNIT_ASSERT_FAILED(
            _anjay_output_set_path((anjay_unlocked_output_ctx_t *) &out,
                                   &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 1)));
}

#undef TEST_ENV

/////////////////////////////////////////////////////////////////////// DECODING

#define TEST_ENV(Size)                                                \
    avs_stream_t *stream = NULL;                                      \
    AVS_UNIT_ASSERT_SUCCESS(avs_unit_memstream_alloc(&stream, Size)); \
    anjay_unlocked_input_ctx_t *in;                                   \
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_text_create(&in, &stream, NULL));

#define TEST_TEARDOWN                                           \
    do {                                                        \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_ctx_destroy(&in)); \
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));   \
    } while (0)

AVS_UNIT_TEST(text_in, string) {
    TEST_ENV(64);

    static const char TEST_STRING[] = "Hello, world!";
    AVS_UNIT_ASSERT_SUCCESS(
            avs_stream_write(stream, TEST_STRING, strlen(TEST_STRING)));

    char buf[64];
    AVS_UNIT_ASSERT_SUCCESS(_anjay_get_string_unlocked(in, buf, sizeof(buf)));
    AVS_UNIT_ASSERT_EQUAL_STRING(buf, TEST_STRING);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(text_in, string_too_long) {
    TEST_ENV(16);

    static const char TEST_STRING[] = "Hello, world!";
    AVS_UNIT_ASSERT_SUCCESS(
            avs_stream_write(stream, TEST_STRING, strlen(TEST_STRING)));

    char buf[8];
    AVS_UNIT_ASSERT_FAILED(_anjay_get_string_unlocked(in, buf, sizeof(buf)));
    AVS_UNIT_ASSERT_EQUAL_STRING(buf, "Hello, "); // assert trailing nullbyte

    TEST_TEARDOWN;
}

#define TEST_NUM_COMMON(Val, ...)                                  \
    do {                                                           \
        TEST_ENV(32);                                              \
                                                                   \
        AVS_UNIT_ASSERT_SUCCESS(                                   \
                avs_stream_write(stream, #Val, sizeof(#Val) - 1)); \
                                                                   \
        __VA_ARGS__;                                               \
                                                                   \
        TEST_TEARDOWN;                                             \
    } while (false)

#define TEST_NUM_FAIL(Type, Getter, Val)                      \
    TEST_NUM_COMMON(Val, Type result; AVS_UNIT_ASSERT_FAILED( \
                            _anjay_get_##Getter##_unlocked(in, &result)))

#define TEST_I64(Val)                                              \
    TEST_NUM_COMMON(Val, int64_t result; AVS_UNIT_ASSERT_SUCCESS(  \
                            _anjay_get_i64_unlocked(in, &result)); \
                    AVS_UNIT_ASSERT_EQUAL(result, (int64_t) (Val##ULL)))

#define TEST_I64_FAIL(Val) TEST_NUM_FAIL(int64_t, i64, Val)

AVS_UNIT_TEST(text_in, i64) {
    TEST_I64(514);
    TEST_I64(0);
    TEST_I64(-1);
    TEST_I64(2147483647);
    TEST_I64(-2147483648);
    TEST_I64(2147483648);
    TEST_I64(-2147483649);
    TEST_I64(9223372036854775807);
    TEST_I64(-9223372036854775808);
    TEST_I64_FAIL(9223372036854775808);
    TEST_I64_FAIL(-9223372036854775809);
    TEST_I64_FAIL(1.0);
    TEST_I64_FAIL(wat);
}

#undef TEST_I64_FAIL
#undef TEST_I64

#define TEST_DOUBLE(Val)                                              \
    TEST_NUM_COMMON(Val, double result; AVS_UNIT_ASSERT_SUCCESS(      \
                            _anjay_get_double_unlocked(in, &result)); \
                    AVS_UNIT_ASSERT_EQUAL(result, (double) Val))

#define TEST_DOUBLE_FAIL(Val) TEST_NUM_FAIL(double, double, Val)

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
    TEST_DOUBLE(4.223e+37);
    TEST_DOUBLE(3.26e+218);
    TEST_DOUBLE_FAIL(wat);
}

#undef TEST_DOUBLE_FAIL
#undef TEST_DOUBLE

#define TEST_BOOL(Val)                                              \
    TEST_NUM_COMMON(Val, bool result; AVS_UNIT_ASSERT_SUCCESS(      \
                            _anjay_get_bool_unlocked(in, &result)); \
                    AVS_UNIT_ASSERT_EQUAL(result, Val))

#define TEST_BOOL_FAIL(Str)                                            \
    do {                                                               \
        TEST_ENV(32);                                                  \
        AVS_UNIT_ASSERT_SUCCESS(                                       \
                avs_stream_write(stream, Str, sizeof(Str) - 1));       \
        bool result;                                                   \
        AVS_UNIT_ASSERT_FAILED(_anjay_get_bool_unlocked(in, &result)); \
        TEST_TEARDOWN;                                                 \
    } while (false);

AVS_UNIT_TEST(text_in, boolean) {
    TEST_BOOL(0);
    TEST_BOOL(1);
    TEST_BOOL_FAIL("2");
    TEST_BOOL_FAIL("-1");
    TEST_BOOL_FAIL("true");
    TEST_BOOL_FAIL("false");
    TEST_BOOL_FAIL("wat");
}

#undef TEST_NUM_FAIL
#undef TEST_NUM_COMMON

#define TEST_OBJLNK_COMMON(Str, ...)                             \
    do {                                                         \
        TEST_ENV(64);                                            \
        AVS_UNIT_ASSERT_SUCCESS(                                 \
                avs_stream_write(stream, Str, sizeof(Str) - 1)); \
        anjay_oid_t oid;                                         \
        anjay_iid_t iid;                                         \
        __VA_ARGS__;                                             \
        TEST_TEARDOWN;                                           \
    } while (false)

#define TEST_OBJLNK(Oid, Iid)                                               \
    TEST_OBJLNK_COMMON(#Oid ":" #Iid,                                       \
                       AVS_UNIT_ASSERT_SUCCESS(                             \
                               _anjay_get_objlnk_unlocked(in, &oid, &iid)); \
                       AVS_UNIT_ASSERT_EQUAL(oid, Oid);                     \
                       AVS_UNIT_ASSERT_EQUAL(iid, Iid))

#define TEST_OBJLNK_FAIL(Str)                  \
    TEST_OBJLNK_COMMON(Str,                    \
                       AVS_UNIT_ASSERT_FAILED( \
                               _anjay_get_objlnk_unlocked(in, &oid, &iid)))

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

#undef TEST_OBJLNK_FAIL
#undef TEST_OBJLNK
#undef TEST_OBJLNK_COMMON
#undef TEST_TEARDOWN
#undef TEST_ENV
