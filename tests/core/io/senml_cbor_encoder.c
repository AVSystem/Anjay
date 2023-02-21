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

#include <math.h>
#include <string.h>

typedef struct cbor_test_env {
    avs_stream_outbuf_t outbuf;
    char *buf; // heap-allocated to make Valgrind check for out-of-bounds access
    anjay_senml_like_encoder_t *encoder;
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
    env->encoder = _anjay_senml_cbor_encoder_new((avs_stream_t *) &env->outbuf);
    AVS_UNIT_ASSERT_NOT_NULL(env->encoder);
}

#define VERIFY_BYTES(Env, Data)                                      \
    do {                                                             \
        AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&Env.outbuf), \
                              sizeof(Data) - 1);                     \
        AVS_UNIT_ASSERT_EQUAL_BYTES(Env.buf, Data);                  \
    } while (0)

AVS_UNIT_TEST(senml_cbor_encoder, empty) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);
    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);
    VERIFY_BYTES(env, "\x80");

    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, integer) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_int(encoder, 100));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env, "\x81\xA1\x02\x18\x64");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, unsigned_integer) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_uint(encoder, 100));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env, "\x81\xA1\x02\x18\x64");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, float) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_double(encoder, 100000.0));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env, "\x81\xA1\x02\xFA\x47\xC3\x50\x00");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, double) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_double(encoder, 1.1));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env, "\x81\xA1\x02\xFB\x3F\xF1\x99\x99\x99\x99\x99\x9A");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, boolean) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_bool(encoder, true));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env, "\x81\xA1\x04\xF5");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, string) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, "senml"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env,
                 "\x81\xA1\x03\x65"
                 "senml");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, bytes) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_bytes_begin(encoder, 5));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_bytes_append(encoder, "\x01\x02", 2));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_bytes_append(encoder, "\x03\x04\x05", 3));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_bytes_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env, "\x81\xA1\x08\x45\x01\x02\x03\x04\x05");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, objlnk) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_objlnk(encoder, "objlnk"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env,
                 "\x81\xA1\x63"
                 "vlo"
                 "\x66"
                 "objlnk");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, basename) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, "bn", NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, "dummy"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env,
                 "\x81\xA2\x21\x62"
                 "bn"
                 "\x03\x65"
                 "dummy");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, name) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, "n", NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, "dummy"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env,
                 "\x81\xA2\x00\x61"
                 "n"
                 "\x03\x65"
                 "dummy");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, basename_and_name) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, "bn", "n", NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, "dummy"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env,
                 "\x81\xA3\x21\x62"
                 "bn"
                 "\x00\x61"
                 "n"
                 "\x03\x65"
                 "dummy");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, basename_name_and_time) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, "bn", "n", 1.0));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, "dummy"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env,
                 "\x81\xA4\x21\x62"
                 "bn"
                 "\x00\x61"
                 "n"
                 "\x22\xFA"
                 "\x3F\x80\x00\x00"
                 "\x03\x65"
                 "dummy");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, two_elements) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_int(encoder, -12));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, NULL, NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, "test"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_element_end(encoder));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    VERIFY_BYTES(env,
                 "\x82\xA1\x02\x2B\xA1\x03\x64"
                 "test");
    avs_free(env.buf);
}

AVS_UNIT_TEST(senml_cbor_encoder, not_closed_element) {
    cbor_test_env_t env;
    cbor_test_setup(&env, 32);

    anjay_senml_like_encoder_t *encoder = env.encoder;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_senml_like_element_begin(encoder, NULL, "n", NAN));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_senml_like_encode_string(encoder, "dummy"));
    AVS_UNIT_ASSERT_FAILED(_anjay_senml_like_encoder_cleanup(&encoder));
    AVS_UNIT_ASSERT_NULL(encoder);

    avs_free(env.buf);
}
