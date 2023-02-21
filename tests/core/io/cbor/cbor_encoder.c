/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_unit_test.h>

#include <avsystem/commons/avs_stream_outbuf.h>
#include <avsystem/commons/avs_utils.h>

static cbor_encoder_t *cbor_encoder_new(avs_stream_t *stream) {
    cbor_encoder_t *ctx =
            (cbor_encoder_t *) avs_calloc(1, sizeof(cbor_encoder_t));
    AVS_UNIT_ASSERT_NOT_NULL(ctx);
    nested_context_push(ctx, stream, CBOR_CONTEXT_TYPE_ROOT);
    return ctx;
}

static int cbor_encoder_delete(cbor_encoder_t **ctx) {
    avs_free(*ctx);
    *ctx = NULL;
    return 0;
}

typedef struct cbor_test_env {
    avs_stream_outbuf_t outbuf;
    char *buf; // heap-allocated to make Valgrind check for out-of-bounds access
    cbor_encoder_t *encoder;
} cbor_test_env_t;

typedef struct {
    const char *data;
    size_t size;
} test_data_t;

#define MAKE_TEST_DATA(Data)     \
    (test_data_t) {              \
        .data = Data,            \
        .size = sizeof(Data) - 1 \
    }

static void cbor_test_setup(cbor_test_env_t *env, size_t buf_size) {
    memcpy(&env->outbuf,
           &AVS_STREAM_OUTBUF_STATIC_INITIALIZER,
           sizeof(avs_stream_outbuf_t));
    env->buf = avs_malloc(buf_size);
    AVS_UNIT_ASSERT_NOT_NULL(env->buf);
    avs_stream_outbuf_set_buffer(&env->outbuf, env->buf, buf_size);
    env->encoder = cbor_encoder_new((avs_stream_t *) &env->outbuf);
}

#define VERIFY_BYTES(Env, Data)                                      \
    do {                                                             \
        AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&Env.outbuf), \
                              sizeof(Data) - 1);                     \
        AVS_UNIT_ASSERT_EQUAL_BYTES(Env.buf, Data);                  \
    } while (0)

static void verify_bytes(cbor_test_env_t *env, test_data_t *data) {
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&env->outbuf), data->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env->buf, data->data, data->size);
}

AVS_UNIT_TEST(cbor_encoder, empty) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);
    VERIFY_BYTES(env, "");

    avs_free(env.buf);
}

static void test_int(int64_t value, test_data_t *data) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(env.encoder, value));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(env.encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&env.encoder));
    AVS_UNIT_ASSERT_NULL(env.encoder);

    verify_bytes(&env, data);
    avs_free(env.buf);
};

#define TEST_INT_IMPL(Name, Num, Data)           \
    AVS_UNIT_TEST(cbor_encoder, Name) {          \
        test_data_t data = MAKE_TEST_DATA(Data); \
        test_int(Num, &data);                    \
    }

#define TEST_INT(Num, Data) TEST_INT_IMPL(AVS_CONCAT(int, __LINE__), Num, Data);

TEST_INT(0, "\x00")
TEST_INT(1, "\x01")
TEST_INT(10, "\x0A")
TEST_INT(23, "\x17")
TEST_INT(24, "\x18\x18")
TEST_INT(25, "\x18\x19")
TEST_INT(100, "\x18\x64")
TEST_INT(221, "\x18\xDD")
TEST_INT(1000, "\x19\x03\xE8")
TEST_INT(INT16_MAX, "\x19\x7F\xFF")
TEST_INT(INT16_MAX + 1, "\x19\x80\x00")
TEST_INT(UINT16_MAX, "\x19\xFF\xFF")
TEST_INT(UINT16_MAX + 1, "\x1A\x00\x01\x00\x00")
TEST_INT(1000000, "\x1A\x00\x0F\x42\x40")
TEST_INT(INT32_MAX, "\x1A\x7F\xFF\xFF\xFF")
TEST_INT((int64_t) INT32_MAX + 1, "\x1A\x80\x00\x00\x00")
TEST_INT(UINT32_MAX, "\x1A\xFF\xFF\xFF\xFF")
TEST_INT((int64_t) UINT32_MAX + 1, "\x1B\x00\x00\x00\x01\x00\x00\x00\x00")
TEST_INT(INT64_MAX, "\x1B\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF")

TEST_INT(-1, "\x20")
TEST_INT(-10, "\x29")
TEST_INT(-24, "\x37")
TEST_INT(-25, "\x38\x18")
TEST_INT(-100, "\x38\x63")
TEST_INT(-256, "\x38\xFF")
TEST_INT(-257, "\x39\x01\x00")
TEST_INT(-1000, "\x39\x03\xE7")
TEST_INT(INT64_MIN, "\x3B\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF")

AVS_UNIT_TEST(cbor_encoder, uint64_max) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_uint(encoder, UINT64_MAX));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    AVS_UNIT_ASSERT_EQUAL_BYTES(env.buf,
                                "\x1B\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF");

    avs_free(env.buf);
}

#define TEST_BOOL(Val, Data)                                         \
    AVS_UNIT_TEST(cbor_encoder, bool_##Val) {                        \
        cbor_test_env_t env;                                         \
        cbor_test_setup(&env, 32);                                   \
        cbor_encoder_t *encoder = env.encoder;                       \
        test_data_t expected = MAKE_TEST_DATA(Data);                 \
                                                                     \
        AVS_UNIT_ASSERT_SUCCESS(cbor_encode_bool(encoder, Val));     \
        AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1); \
        AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));      \
        verify_bytes(&env, &expected);                               \
                                                                     \
        avs_free(env.buf);                                           \
    }

TEST_BOOL(true, "\xF5")
TEST_BOOL(false, "\xF4")
TEST_BOOL(1, "\xF5")
TEST_BOOL(0, "\xF4")
TEST_BOOL(42, "\xF5")

static void test_string(const char *input, test_data_t *expected) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 512);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_string(encoder, input));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    verify_bytes(&env, expected);

    avs_free(env.buf);
}

#define TEST_STRING_NAMED(Name, Text, ExpectedHeader)               \
    AVS_UNIT_TEST(cbor_encoder, string_##Name) {                    \
        test_data_t expected = MAKE_TEST_DATA(ExpectedHeader Text); \
        test_string(Text, &expected);                               \
    }

#define TEST_STRING(Text, ExpectedHeader) \
    TEST_STRING_NAMED(Text, AVS_QUOTE(Text), ExpectedHeader)

TEST_STRING(, "\x60")
TEST_STRING(a, "\x61")
TEST_STRING(IETF, "\x64")
TEST_STRING_NAMED(dzborg, "DZBORG:DD", "\x69")
TEST_STRING_NAMED(escaped, "\"\\", "\x62")
TEST_STRING_NAMED(
        255chars,
        "oxazxnwrmthhloqwchkumektviptdztidxeelvgffcdoodpijsbikkkvrmtrxddmpidudj"
        "ptfmqqgfkjlrsqrmagculcyjjbmxombbiqdhimwafcfaswhmmykezictjpidmxtoqnjmja"
        "xzgvqdybtgneqsmlzhxqeuhibjopnregwykgpcdogguszhhffdeixispwfnwcufnmsxycy"
        "qxquiqsuqwgkwafkeedsacxvvjwhpokaabxelqxzqutwa",
        "\x78\xFF")
TEST_STRING_NAMED(
        256chars,
        "oqndmcvrgmvswuvcskllakhhersslftmmuwwwzirelnbtnlmvmezrqktqqnlpldqwyvtbv"
        "yryqcurqxnhzxoladzzmnumrifhqbcywuetmuyyjxpiwquzrekjxzgiknqcmwzwuzxvrxb"
        "zycnfrhyigwgkmbtlfyrhkolnsikvdelvkztkvonimtmvrivrnevgyxvjdjzvobsiufbwt"
        "atfqeavhvfdfbnsumtletbaheyacrkwgectlrdrizenuvi",
        "\x79\x01\x00")

static void test_float(float value, test_data_t *expected) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_double(encoder, value));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    verify_bytes(&env, expected);

    avs_free(env.buf);
}

#define TEST_FLOAT_IMPL(Name, Type, Num, Data)       \
    AVS_UNIT_TEST(cbor_encoder, Name) {              \
        test_data_t expected = MAKE_TEST_DATA(Data); \
        test_float(Num, &expected);                  \
    }

#define TEST_FLOAT(Num, Data) \
    TEST_FLOAT_IMPL(AVS_CONCAT(float, __LINE__), float, Num, Data)

// TODO: Make those tests work as defined in RFC7049 Appendix C, it requires
// half floats support.
// TEST_FLOAT(0.0, "\xF9\x00\x00")
// TEST_FLOAT(-0.0, "\xF9\x80\x00")
// TEST_FLOAT(1.0, "\xF9\x3C\x00")
TEST_FLOAT(-0.0, "\xFA\x80\x00\x00\x00")
TEST_FLOAT(100000.0, "\xFA\x47\xC3\x50\x00")

static void test_double(double value, test_data_t *expected) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_double(encoder, value));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    verify_bytes(&env, expected);

    avs_free(env.buf);
}

#define TEST_DOUBLE_IMPL(Name, Type, Num, Data)      \
    AVS_UNIT_TEST(cbor_encoder, Name) {              \
        test_data_t expected = MAKE_TEST_DATA(Data); \
        test_double(Num, &expected);                 \
    }

#define TEST_DOUBLE(Num, Data) \
    TEST_DOUBLE_IMPL(AVS_CONCAT(double, __LINE__), double, Num, Data)

TEST_DOUBLE(1.1, "\xFB\x3F\xF1\x99\x99\x99\x99\x99\x9A")
TEST_DOUBLE(100000.0, "\xFA\x47\xC3\x50\x00")
TEST_DOUBLE(1.0e+300, "\xFB\x7E\x37\xE4\x3C\x88\x00\x75\x9C")
TEST_DOUBLE(-4.1, "\xFB\xC0\x10\x66\x66\x66\x66\x66\x66")

static void test_bytes(test_data_t *input, test_data_t *expected) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 512);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_begin(encoder, input->size));
    AVS_UNIT_ASSERT_SUCCESS(
            cbor_bytes_append(encoder, input->data, input->size));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    verify_bytes(&env, expected);

    avs_free(env.buf);
}

#define TEST_BYTES(Name, Data, ExpectedHeader)                      \
    AVS_UNIT_TEST(cbor_encoder, bytes_##Name) {                     \
        test_data_t input = MAKE_TEST_DATA(Data);                   \
        test_data_t expected = MAKE_TEST_DATA(ExpectedHeader Data); \
        test_bytes(&input, &expected);                              \
    }

TEST_BYTES(0bytes, "", "\x40")
TEST_BYTES(4bytes, "\x01\x02\x03\x04", "\x44")
TEST_BYTES(5bytes, "\x64\x49\x45\x54\x46", "\x45")
TEST_BYTES(23bytes,
           "\x84\x11\xDB\xB8\xAA\xF7\xC3\xEF\xBA\xC0\x2F\x50\xC2\x88\xAF\x1B"
           "\x8F\xD2\xE4\xC9\x5A\xD7\xEC",
           "\x57")
TEST_BYTES(24bytes,
           "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
           "\x3F\xAC\x35\x03\x16\x1E\x32\x0A",
           "\x58\x18")
TEST_BYTES(
        255bytes,
        "\xD6\xFB\x20\x80\xCE\x44\x31\x3B\xE1\x63\xD9\x89\x36\x90\x06\x56\x9C"
        "\xF6\x4C\x24\x04\x34\xEA\x8D\xF3\xF1\x40\xEA\x3A\x41\xE1\x57\xFF\x92"
        "\xCC\xAE\x42\x10\x27\x48\x47\x6E\x7C\x11\x9B\x5A\x21\x5A\x51\xF7\x45"
        "\xB0\x5E\x3B\x81\x26\xE9\xB0\x8A\xF1\x93\xCA\xA6\xB3\xD7\xE0\x16\xEC"
        "\xBF\xF5\x21\x16\xC7\x50\x6C\x9A\xA8\x8E\x49\xA9\xF1\x59\x8C\xC3\x80"
        "\x0F\x34\x21\x26\xCD\xB5\x30\xEE\xC5\x48\xBB\x6F\x03\x62\xC2\x7B\x21"
        "\x60\x08\xE2\x58\xD3\xE0\x64\x3A\x4B\x59\x16\xFD\x8E\x05\x41\x46\xBD"
        "\xFB\xC8\x7B\x4D\xC3\x38\x01\x94\x31\x50\xFC\xE7\xBE\x7A\xDA\xD6\x56"
        "\x74\x1C\x7F\x75\xB1\x59\x15\x4E\x86\x8E\x71\xB0\xFF\x69\x60\xDC\xBC"
        "\x52\xB6\xEA\xFA\x4E\x09\xD3\xB8\x40\x85\x7D\xDA\xB1\xC8\xFF\x65\xB7"
        "\xFF\xA9\xAB\x9E\x67\x04\x0A\x3A\x1B\xE7\x77\x53\x9A\xA1\x6D\xDA\xA0"
        "\xBB\xC0\x91\xA1\x38\x93\x0E\x33\xDF\x4B\x9E\x83\x0C\xF4\x73\x1E\xD6"
        "\x83\x92\x54\x3D\x73\x1F\xEC\xCA\xD9\x1F\xE2\x3D\x57\xD1\x7C\x54\x88"
        "\xFB\x3E\xCF\x7E\x8A\x29\x98\x89\x4A\xBB\x2F\xE5\xB1\x36\x2B\x8B\x8F"
        "\xBF\x46\x19\x74\x1D\xC4\x7B\xFB\x52\xA4\x32\x47\xA7\x5C\xA1\x5C\x1A",
        "\x58\xFF")
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
           "\x59\x01\x00")

// {}
AVS_UNIT_TEST(cbor_encoder, empty_map) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_begin(encoder, 0));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\xA0");

    avs_free(env.buf);
}

// {"a": 1}
AVS_UNIT_TEST(cbor_encoder, map1el) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_begin(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_string(encoder, "a"));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\xA1\x61\x61\x01");

    avs_free(env.buf);
}

// {1.1: "test", 256: 65536}
AVS_UNIT_TEST(cbor_encoder, map2el) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_begin(encoder, 2));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_double(encoder, 1.1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_string(encoder, "test"));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 256));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 65536));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env,
                 "\xA2\xFB\x3F\xF1\x99\x99\x99\x99\x99\x9A\x64\x74\x65\x73\x74"
                 "\x19\x01\x00\x1A\x00\x01\x00\x00");

    avs_free(env.buf);
}

// DEFINITE ARRAY TESTS

// [1, "cwiercz", 200]
AVS_UNIT_TEST(cbor_encoder, definite_array) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_string(encoder, "cwiercz"));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 200));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\x83\x01\x67\x63\x77\x69\x65\x72\x63\x7A\x18\xC8");

    avs_free(env.buf);
}

// []
AVS_UNIT_TEST(cbor_encoder, empty_definite_array) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));

    VERIFY_BYTES(env, "\x80");

    avs_free(env.buf);
}

// [1, [2]]
AVS_UNIT_TEST(cbor_encoder, nested_definite_arrays1) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 2));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));

    VERIFY_BYTES(env, "\x82\x01\x81\x02");

    avs_free(env.buf);
}

// [1, 2, [3, 4, 5]]
AVS_UNIT_TEST(cbor_encoder, nested_definite_arrays2) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 2));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 4));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 5));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));

    VERIFY_BYTES(env, "\x83\x01\x02\x83\x03\x04\x05");

    avs_free(env.buf);
}

// {[h'00', h'11']}
AVS_UNIT_TEST(cbor_encoder, map_with_array_with_bytes) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_begin(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_string(encoder, "array"));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_begin(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_append(encoder, "\x00", 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_begin(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_append(encoder, "\x11", 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));

    VERIFY_BYTES(env, "\xA1\x65\x61\x72\x72\x61\x79\x82\x41\x00\x41\x11");

    avs_free(env.buf);
}

// [[]]
AVS_UNIT_TEST(cbor_encoder, empty_nested_arrays) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));

    VERIFY_BYTES(env, "\x81\x80");

    avs_free(env.buf);
}

// [1, [2, [3]]]
AVS_UNIT_TEST(cbor_encoder, double_nested_arrays) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 2));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));

    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\x82\x01\x82\x02\x81\x03");

    avs_free(env.buf);
}

// [[1, 2], [3, 4], [5, 6]]
AVS_UNIT_TEST(cbor_encoder, three_nested_arrays) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_uint(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 2));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_uint(encoder, 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 4));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_uint(encoder, 5));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 6));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));

    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\x83\x82\x01\x02\x82\x03\x04\x82\x05\x06");

    avs_free(env.buf);
}

// [{"A": 1}]
AVS_UNIT_TEST(cbor_encoder, array_with_one_map) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_begin(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_string(encoder, "A"));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));

    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\x81\xA1\x61\x41\x01");

    avs_free(env.buf);
}

// [{"A": 1}, {"B": 2}]
AVS_UNIT_TEST(cbor_encoder, array_with_two_maps) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_begin(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_string(encoder, "A"));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_begin(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_string(encoder, "B"));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 2));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));

    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\x82\xA1\x61\x41\x01\xA1\x61\x42\x02");

    avs_free(env.buf);
}

// [h'AABBCC', h'DDEEFF']
AVS_UNIT_TEST(cbor_encoder, array_with_bytes) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_begin(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_begin(encoder, 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_append(encoder, "\xAA\xBB\xCC", 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_begin(encoder, 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_append(encoder, "\xDD\xEE\xFF", 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_array_end(encoder));

    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\x82\x43\xAA\xBB\xCC\x43\xDD\xEE\xFF");

    avs_free(env.buf);
}

/* Some invalid inputs */

AVS_UNIT_TEST(cbor_encoder, too_few_bytes) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_begin(encoder, 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_append(encoder, "\x00", 1));
    // This should fail and close bytes context
    AVS_UNIT_ASSERT_FAILED(cbor_bytes_end(encoder));
    AVS_UNIT_ASSERT_EQUAL(encoder->stack_size, 1);

    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));

    avs_free(env.buf);
}

// h'000102'
AVS_UNIT_TEST(cbor_encoder, too_many_bytes) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_begin(encoder, 3));
    // This should fail and don't modify bytes context
    AVS_UNIT_ASSERT_FAILED(cbor_bytes_append(encoder, "\x00\x01\x02\x03", 4));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_append(encoder, "\x00\x01\x02", 3));
    AVS_UNIT_ASSERT_SUCCESS(cbor_bytes_end(encoder));

    AVS_UNIT_ASSERT_EQUAL(nested_context_top(encoder)->size, 1);
    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));
    VERIFY_BYTES(env, "\x43\x00\x01\x02");

    avs_free(env.buf);
}

// {_ 1234:
AVS_UNIT_TEST(cbor_encoder, invalid_number_of_elements_in_map) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    cbor_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(cbor_definite_map_begin(encoder, 1));
    AVS_UNIT_ASSERT_SUCCESS(cbor_encode_int(encoder, 1234));
    AVS_UNIT_ASSERT_FAILED(cbor_definite_map_end(encoder));

    AVS_UNIT_ASSERT_SUCCESS(cbor_encoder_delete(&encoder));

    avs_free(env.buf);
}
