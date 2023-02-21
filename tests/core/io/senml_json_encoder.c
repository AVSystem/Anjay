/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#include <anjay_init.h>

#include <avsystem/commons/avs_unit_test.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_stream_outbuf.h>
#include <avsystem/commons/avs_utils.h>

#include "src/core/io/anjay_senml_like_encoder.h"

#include "src/core/coap/anjay_content_format.h"

#include <math.h>
#include <string.h>

typedef struct json_test_env {
    avs_stream_outbuf_t outbuf;
    char *buf; // heap-allocated to make Valgrind check for out-of-bounds access
    anjay_senml_like_encoder_t *encoder;
} json_test_env_t;

typedef struct {
    const char *data;
    size_t size;
} test_data_t;

#define MAKE_TEST_DATA(Data)     \
    (test_data_t) {              \
        .data = Data,            \
        .size = sizeof(Data) - 1 \
    }

static void json_test_setup(json_test_env_t *env, size_t buf_size) {
    memcpy(&env->outbuf,
           &AVS_STREAM_OUTBUF_STATIC_INITIALIZER,
           sizeof(avs_stream_outbuf_t));
    env->buf = avs_calloc(1, buf_size);
    AVS_UNIT_ASSERT_NOT_NULL(env->buf);
    avs_stream_outbuf_set_buffer(&env->outbuf, env->buf, buf_size);
    env->encoder = _anjay_senml_json_encoder_new((avs_stream_t *) &env->outbuf);
    AVS_UNIT_ASSERT_NOT_NULL(env->encoder);
}

#define VERIFY_BYTES(Env, Data)                                      \
    do {                                                             \
        AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&Env.outbuf), \
                              sizeof(Data) - 1);                     \
        AVS_UNIT_ASSERT_EQUAL_BYTES(Env.buf, Data);                  \
    } while (0)

static void verify_bytes(json_test_env_t *env, test_data_t *data) {
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&env->outbuf), data->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env->buf, data->data, data->size);
}

AVS_UNIT_TEST(senml_json_encoder, empty) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env, "[]");
    avs_free(env.buf);
}

static void test_int(int64_t value, test_data_t *data) {
    json_test_env_t env;
    json_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_int(encoder, value));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    verify_bytes(&env, data);
    avs_free(env.buf);
};

#define TEST_INT_IMPL(Name, Num, Data)           \
    AVS_UNIT_TEST(senml_json_encoder, Name) {    \
        test_data_t data = MAKE_TEST_DATA(Data); \
        test_int(Num, &data);                    \
    }

#define TEST_INT(Num, Data) TEST_INT_IMPL(AVS_CONCAT(int, __LINE__), Num, Data);

TEST_INT(0, "[{\"v\":0}]")
TEST_INT(INT16_MAX, "[{\"v\":32767}]")
TEST_INT(UINT16_MAX, "[{\"v\":65535}]")
TEST_INT(INT32_MAX, "[{\"v\":2147483647}]")
TEST_INT(UINT32_MAX, "[{\"v\":4294967295}]")
TEST_INT(INT64_MAX, "[{\"v\":9223372036854775807}]")
TEST_INT(-1, "[{\"v\":-1}]")
TEST_INT(INT64_MIN, "[{\"v\":-9223372036854775808}]")

AVS_UNIT_TEST(senml_json_encoder, uint64_max) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_uint(encoder, UINT64_MAX));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env, "[{\"v\":18446744073709551615}]");
    avs_free(env.buf);
}

#define TEST_BOOL(Val, Data)                                                  \
    AVS_UNIT_TEST(senml_json_encoder, bool_##Val) {                           \
        json_test_env_t env;                                                  \
        json_test_setup(&env, 32);                                            \
        anjay_senml_like_encoder_t *encoder = env.encoder;                    \
        test_data_t expected = MAKE_TEST_DATA(Data);                          \
                                                                              \
        AVS_UNIT_ASSERT_SUCCESS(                                              \
                _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));   \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_bool(encoder, Val)); \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));      \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder)); \
                                                                              \
        verify_bytes(&env, &expected);                                        \
        avs_free(env.buf);                                                    \
    }

TEST_BOOL(true, "[{\"vb\":true}]")
TEST_BOOL(false, "[{\"vb\":false}]")
TEST_BOOL(1, "[{\"vb\":true}]")
TEST_BOOL(0, "[{\"vb\":false}]")
TEST_BOOL(42, "[{\"vb\":true}]")

AVS_UNIT_TEST(senml_json_encoder, simple_element) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;
    test_data_t expected = MAKE_TEST_DATA("[{\"v\":123}]");

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_int(encoder, 123));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    verify_bytes(&env, &expected);
    avs_free(env.buf);
}

static void test_string(const char *input, test_data_t *expected) {
    json_test_env_t env;
    json_test_setup(&env, 512);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, input));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    verify_bytes(&env, expected);
    avs_free(env.buf);
}

#define TEST_STRING_NAMED_EXPLICIT(Name, Text, Expected) \
    AVS_UNIT_TEST(senml_json_encoder, string_##Name) {   \
        test_data_t expected = MAKE_TEST_DATA(Expected); \
        test_string(Text, &expected);                    \
    }

#define TEST_STRING_NAMED(Name, Text) \
    TEST_STRING_NAMED_EXPLICIT(Name, Text, "[{\"vs\":\"" Text "\"}]")

TEST_STRING_NAMED(empty, "")
TEST_STRING_NAMED(
        256chars,
        "oxazxnwrmthhloqwchkumektviptdztidxeelvgffcdoodpijsbikkkvrmtrxddmpidudj"
        "ptfmqqgfkjlrsqrmagculcyjjbmxombbiqdhimwafcfaswhmmykezictjpidmxtoqnjmja"
        "xzgvqdybtgneqsmlzhxqeuhibjopnregwykgpcdogguszhhffdeixispwfnwcufnmsxycy"
        "qxquiqsuqwgkwafkeedsacxvvjwhpokaabxelqxzqutwab");
TEST_STRING_NAMED_EXPLICIT(escaped, "\"\\", "[{\"vs\":\"\\\"\\\\\"}]")
TEST_STRING_NAMED_EXPLICIT(del, "\x7F", "[{\"vs\":\"\\u007f\"}]")

static void test_float(float value) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_double(encoder, value));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    AVS_UNIT_ASSERT_EQUAL(strlen(env.buf),
                          avs_stream_outbuf_offset(&env.outbuf));
    float decoded;
    char brace, bracket, nullbyte;
    AVS_UNIT_ASSERT_EQUAL(sscanf(env.buf,
                                 "[{\"v\":%f%c%c%c",
                                 &decoded,
                                 &brace,
                                 &bracket,
                                 &nullbyte),
                          3);
    AVS_UNIT_ASSERT_EQUAL(decoded, value);
    AVS_UNIT_ASSERT_EQUAL(brace, '}');
    AVS_UNIT_ASSERT_EQUAL(bracket, ']');

    avs_free(env.buf);
}

#define TEST_FLOAT_IMPL(Name, Type, Num)      \
    AVS_UNIT_TEST(senml_json_encoder, Name) { \
        test_float(Num);                      \
    }

#define TEST_FLOAT(Num) TEST_FLOAT_IMPL(AVS_CONCAT(float, __LINE__), float, Num)

TEST_FLOAT(0.0)
TEST_FLOAT(-0.0)
TEST_FLOAT(1.0)
TEST_FLOAT(100000.0)
TEST_FLOAT(1.125)

static void test_double(double value) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_double(encoder, value));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    AVS_UNIT_ASSERT_EQUAL(strlen(env.buf),
                          avs_stream_outbuf_offset(&env.outbuf));
    double decoded;
    char brace, bracket, nullbyte;
    AVS_UNIT_ASSERT_EQUAL(sscanf(env.buf,
                                 "[{\"v\":%lf%c%c%c",
                                 &decoded,
                                 &brace,
                                 &bracket,
                                 &nullbyte),
                          3);
    AVS_UNIT_ASSERT_EQUAL(decoded, value);
    AVS_UNIT_ASSERT_EQUAL(brace, '}');
    AVS_UNIT_ASSERT_EQUAL(bracket, ']');

    avs_free(env.buf);
}

#define TEST_DOUBLE_IMPL(Name, Type, Num)     \
    AVS_UNIT_TEST(senml_json_encoder, Name) { \
        test_double(Num);                     \
    }

#define TEST_DOUBLE(Num) \
    TEST_DOUBLE_IMPL(AVS_CONCAT(double, __LINE__), double, Num)

TEST_DOUBLE(1.1)
TEST_DOUBLE(100000.0)
TEST_DOUBLE(1.0e+300)
TEST_DOUBLE(-4.1)

static void test_bytes(test_data_t *input, test_data_t *expected) {
    json_test_env_t env;
    json_test_setup(&env, 512);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_bytes_begin(encoder, input->size));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_bytes_append(encoder, input->data, input->size));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_bytes_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    verify_bytes(&env, expected);
    avs_free(env.buf);
}

#define TEST_BYTES(Name, Data, Expected)                 \
    AVS_UNIT_TEST(senml_json_encoder, bytes_##Name) {    \
        test_data_t input = MAKE_TEST_DATA(Data);        \
        test_data_t expected = MAKE_TEST_DATA(Expected); \
        test_bytes(&input, &expected);                   \
    }

TEST_BYTES(0bytes, "", "[{\"vd\":\"\"}]")
TEST_BYTES(4bytes, "\x01\x02\x03\x04", "[{\"vd\":\"AQIDBA\"}]")
TEST_BYTES(256bytes,
           "\xD8\xE2\xE6\xED\x90\x05\x29\x3B\x17\xAC\x8D\x33\x93\x52\xD9\x6B"
           "\xF2\xFB\x20\x74\x3E\x9C\xEF\xAD\xBB\x03\xCE\x0E\xC5\xBD\x0D\x2F"
           "\x42\x6D\x1C\xD6\xDB\x29\xF8\xF6\xA4\x96\x3D\x7A\x8A\xEE\xE6\xF2"
           "\x56\x1C\xBE\xCE\x71\x30\x3B\xEC\xC9\x86\x71\x96\x86\x51\xA2\xCA"
           "\x23\x8A\x0B\x1D\x67\x3C\x50\xB8\x66\x4C\x64\x8C\x31\xCD\x11\x05"
           "\xCA\x56\x4B\xBB\x79\x18\x8F\x5B\xF1\xE0\x1E\x85\x38\xBE\x7A\x6F"
           "\x30\x4A\xFD\xB3\x1B\xA9\x52\xB4\x0E\x95\x73\x83\xA5\x33\x9F\x0C"
           "\x04\x2E\x33\xB3\xD5\x0B\x6E\x02\x0C\xC7\x0D\x1A\x1A\x48\x0C\x92"
           "\x1B\x62\x83\xCF\xC1\x5C\x90\xBC\x83\x3B\x92\xBF\x8E\xCE\x7C\xD6"
           "\x99\x77\xF2\x66\x92\x0C\xC6\x0A\x11\x80\xBE\x03\x59\x23\x89\xF6"
           "\xEF\x3A\x5A\x07\xEB\xEF\x47\xF0\x1F\xF0\xB4\x96\x01\x1B\xE9\x51"
           "\x40\x70\x16\xDD\xB2\x9B\xEB\x42\xAC\x6E\x45\xE6\xAE\x8F\xCE\x9A"
           "\xC4\xCB\x09\xE7\x2C\xE4\x48\x86\xF0\x9C\x56\x2C\xEF\x1B\xD0\x8E"
           "\x92\xD4\x61\x15\x46\x76\x19\x32\xDF\x9F\x98\xC0\x0A\xF7\xAE\xA9"
           "\xD7\x61\xEC\x8B\x78\xE5\xAA\xC6\x0B\x5D\x98\x1D\x86\xE6\x57\x67"
           "\x97\x56\x82\x29\xFF\x8F\x61\x6C\xA5\xD0\x08\x20\xAE\x49\x5B\x04",
           "[{\"vd\":\"2OLm7ZAFKTsXrI0zk1LZa_"
           "L7IHQ-nO-tuwPODsW9DS9CbRzW2yn49qSWPXqK7ubyVhy-znEwO-"
           "zJhnGWhlGiyiOKCx1nPFC4ZkxkjDHNEQXKVku7eRiPW_"
           "HgHoU4vnpvMEr9sxupUrQOlXODpTOfDAQuM7PVC24CDMcNGhpIDJIbYoPPwVyQvIM7k"
           "r-OznzWmXfyZpIMxgoRgL4DWSOJ9u86Wgfr70fwH_"
           "C0lgEb6VFAcBbdspvrQqxuReauj86axMsJ5yzkSIbwnFYs7xvQjpLUYRVGdhky35-"
           "YwAr3rqnXYeyLeOWqxgtdmB2G5ldnl1aCKf-PYWyl0AggrklbBA\"}]")

AVS_UNIT_TEST(senml_json_encoder, objlnk) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_encode_objlnk(encoder, "012345:678901"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    VERIFY_BYTES(env, "[{\"vlo\":\"012345:678901\"}]");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_json_encoder, time) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, 1.234));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    VERIFY_BYTES(env, "[{\"bt\":1.234}]");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_json_encoder, array_with_one_empty_element) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    VERIFY_BYTES(env, "[{}]");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_json_encoder, array_with_two_empty_elements) {
    json_test_env_t env;
    json_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    VERIFY_BYTES(env, "[{},{}]");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_json_encoder, array_with_four_elements) {
    json_test_env_t env;
    json_test_setup(&env, 256);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, "basename", NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_int(encoder, 123));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, "basename", "name", NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_double(encoder, 1.0));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, "name", NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_bool(encoder, true));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_begin(
            encoder, "basename", "name", 2.125));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, "dummy"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));

    VERIFY_BYTES(env,
                 "[{\"bn\":\"basename\",\"v\":123},{\"bn\":\"basename\","
                 "\"n\":\"name\",\"v\":1},{\"n\":\"name\",\"vb\":true},{\"bn\":"
                 "\"basename\",\"n\":\"name\",\"bt\":2.125,\"vs\":\"dummy\"}]");

    avs_free(env.buf);
}
