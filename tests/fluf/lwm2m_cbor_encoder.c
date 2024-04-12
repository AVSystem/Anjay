/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <fluf/fluf_config.h>

#ifdef FLUF_WITH_LWM2M_CBOR

#    include <math.h>
#    include <string.h>

#    include <avsystem/commons/avs_unit_test.h>

#    include <fluf/fluf_io.h>
#    include <fluf/fluf_utils.h>

#    include "../../src/fluf/fluf_cbor_encoder.h"

typedef struct {
    fluf_io_out_ctx_t ctx;
    fluf_op_t op_type;
    char buf[500];
    size_t buffer_length;
    size_t out_length;
} lwm2m_cbor_test_env_t;

static void lwm2m_cbor_test_setup(lwm2m_cbor_test_env_t *env,
                                  fluf_uri_path_t *base_path,
                                  size_t items_count,
                                  fluf_op_t op_type) {
    env->buffer_length = sizeof(env->buf);
    env->out_length = 0;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_out_ctx_init(&env->ctx, op_type, base_path, items_count,
                                 FLUF_COAP_FORMAT_OMA_LWM2M_CBOR));
}

#    define VERIFY_BYTES(Env, Data)                                  \
        do {                                                         \
            AVS_UNIT_ASSERT_EQUAL_BYTES(Env.buf, Data);              \
            AVS_UNIT_ASSERT_EQUAL(Env.out_length, sizeof(Data) - 1); \
        } while (0)

AVS_UNIT_TEST(lwm2m_cbor_encoder, send_single_record) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {3: {3: {3: 25}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, read_single_resource_record) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, &FLUF_MAKE_OBJECT_PATH(3), 1, FLUF_OP_DM_READ);
    fluf_io_out_entry_t entry = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {3: {3: {3: 25}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF");

    lwm2m_cbor_test_setup(&env, &FLUF_MAKE_INSTANCE_PATH(3, 3), 1,
                          FLUF_OP_DM_READ);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {3: {3: {3: 25}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF");

    lwm2m_cbor_test_setup(&env, &FLUF_MAKE_RESOURCE_PATH(3, 3, 3), 1,
                          FLUF_OP_DM_READ);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {3: {3: {3: 25}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, read_single_resource_instance_record) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, &FLUF_MAKE_OBJECT_PATH(3), 1, FLUF_OP_DM_READ);
    fluf_io_out_entry_t entry = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {3: {3: {3: {3: 25}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF\xFF");

    lwm2m_cbor_test_setup(&env, &FLUF_MAKE_INSTANCE_PATH(3, 3), 1,
                          FLUF_OP_DM_READ);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {3: {3: {3: {3: 25}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF\xFF");

    lwm2m_cbor_test_setup(&env, &FLUF_MAKE_RESOURCE_PATH(3, 3, 3), 1,
                          FLUF_OP_DM_READ);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {3: {3: {3: {3: 25}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF\xFF");

    lwm2m_cbor_test_setup(&env, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 3, 3),
                          1, FLUF_OP_DM_READ);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {3: {3: {3: {3: 25}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, send_two_records_different_obj) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: 25}}, 1: {1: {1: 11}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF"
                      "\x01\xBF\x01\xBF\x01"
                      "\x0B"
                      "\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, send_two_records_different_inst) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 1, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: 25}, 1: {1: 11}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF"
                      "\x01\xBF\x01"
                      "\x0B"
                      "\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, send_two_records_different_res) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: 25, 1: 11}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\x01"
                      "\x0B"
                      "\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, send_two_resource_instances) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 3, 0),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 3, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: {0: 25, 1: 11}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03\xBF\x00"
                      "\x18\x19"
                      "\x01"
                      "\x0B"
                      "\xFF\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, send_two_resource_instances_different_res) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 3, 0),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 1, 0),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: {0: 25}, 1: {0: 11}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03\xBF\x00"
                      "\x18\x19"
                      "\xFF\x01\xBF\x00"
                      "\x0B"
                      "\xFF\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, send_two_resource_instances_different_inst) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 3, 0),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 1, 0, 0),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: {0: 25}}, 1: {0: {0: 11}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03\xBF\x00"
                      "\x18\x19"
                      "\xFF\xFF\x01\xBF\x00\xBF\x00"
                      "\x0B"
                      "\xFF\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, send_two_resource_instances_different_obj) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 3, 0),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 0, 0, 0),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: {0: 25}}}, 1: {0: {0: {0: 11}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03\xBF\x00"
                      "\x18\x19"
                      "\xFF\xFF\xFF\x01\xBF\x00\xBF\x00\xBF\x00"
                      "\x0B"
                      "\xFF\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder,
              send_two_records_different_level_different_res) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 3, 1, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: 25, 1: {1: 11}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\x01\xBF\x01"
                      "\x0B"
                      "\xFF\xFF\xFF\xFF");

    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);
    out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {1: {1: 11}, 3: 25}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x01\xBF\x01"
                      "\x0B\xFF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder,
              send_two_records_different_level_different_inst) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 1, 1, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: 25}, 1: {1: {1: 11}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\x01\xBF\x01\xBF\x01"
                      "\x0B"
                      "\xFF\xFF\xFF\xFF");

    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);
    out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {1: {1: {1: 11}}, 3: {3: 25}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x01\xBF\x01\xBF\x01"
                      "\x0B\xFF\xFF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder,
              send_two_records_different_level_different_obj) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 1, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {3: {3: {3: 25}}, 1: {1: {1: {1: 11}}}}
    VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\x01\xBF\x01\xBF\x01\xBF\x01"
                      "\x0B"
                      "\xFF\xFF\xFF\xFF");

    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);
    out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {1: {1: {1: {1: 11}}}, 3: {3: {3: 25}}}
    VERIFY_BYTES(env, "\xBF\x01\xBF\x01\xBF\x01\xBF\x01"
                      "\x0B\xFF\xFF\xFF\x03\xBF\x03\xBF\x03"
                      "\x18\x19"
                      "\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, biggest_possible_record) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(65534, 65534, 65534, 65534),
        .type = FLUF_DATA_TYPE_OBJLNK,
        .value.objlnk.iid = 65534,
        .value.objlnk.oid = 65534
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    // {65534: {65534: {65534: {65534: "65534:65534"}}}}
    VERIFY_BYTES(env, "\xBF\x19\xFF\xFE\xBF\x19\xFF\xFE\xBF\x19\xFF\xFE\xBF"
                      "\x19\xFF\xFE"
                      "\x6B\x36\x35\x35\x33\x34\x3A\x36\x35\x35\x33\x34"
                      "\xFF\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, biggest_possible_second_record) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(65533, 65533, 65533, 65533),
        .type = FLUF_DATA_TYPE_OBJLNK,
        .value.objlnk.iid = 65534,
        .value.objlnk.oid = 65534
    };
    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(65534, 65534, 65534, 65534),
        .type = FLUF_DATA_TYPE_OBJLNK,
        .value.objlnk.iid = 65534,
        .value.objlnk.oid = 65534
    };

    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, &env.buf[env.out_length],
            env.buffer_length - env.out_length, &out_len));
    env.out_length += out_len;
    // {65533: {65533: {65533: {65533: "65534:65534"}}}, 65534: {65534: {65534:
    // {65534: "65534:65534"}}}}
    VERIFY_BYTES(env,
                 "\xBF\x19\xFF\xFD\xBF\x19\xFF\xFD"
                 "\xBF\x19\xFF\xFD\xBF\x19\xFF\xFD"
                 "\x6B\x36\x35\x35\x33\x34\x3A\x36\x35\x35\x33\x34"
                 "\xFF\xFF\xFF"
                 "\x19\xFF\xFE\xBF\x19\xFF\xFE\xBF\x19\xFF\xFE\xBF\x19\xFF\xFE"
                 "\x6B\x36\x35\x35\x33\x34\x3A\x36\x35\x35\x33\x34"
                 "\xFF\xFF\xFF\xFF");
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, single_record_chunk_read) {
    lwm2m_cbor_test_env_t env = { 0 };

    fluf_io_out_entry_t entry = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_DOUBLE,
        .value.double_value = 1.1
    };

    for (size_t chunk_len = 1; chunk_len < 18; chunk_len++) {
        size_t out_len = 0;
        lwm2m_cbor_test_setup(&env, &FLUF_MAKE_OBJECT_PATH(3), 1,
                              FLUF_OP_DM_READ);
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
        int res = -1;
        while (res) {
            res = fluf_io_out_ctx_get_payload(
                    &env.ctx, &env.buf[env.out_length], chunk_len, &out_len);
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
            env.out_length += out_len;
        }
        // {3: {3: {3: 1.1}}}
        VERIFY_BYTES(env, "\xBF\x03\xBF\x03\xBF\x03"
                          "\xFB\x3F\xF1\x99\x99\x99\x99\x99\x9A"
                          "\xFF\xFF\xFF");
    }
}

static char *ptr_for_callback = "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEE";
static int external_data_handler(void *buffer,
                                 size_t bytes_to_copy,
                                 size_t offset,
                                 void *args) {
    (void) args;
    memcpy(buffer, &ptr_for_callback[offset], bytes_to_copy);
    return 0;
}

static fluf_io_out_entry_t entries[] = {
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 0),
        .type = FLUF_DATA_TYPE_INT,
        .value.int_value = 25,
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 100,
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 2),
        .type = FLUF_DATA_TYPE_TIME,
        .value.time_value = 3,
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 3),
        .type = FLUF_DATA_TYPE_STRING,
        .value.bytes_or_string.data =
                "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 4),
        .type = FLUF_DATA_TYPE_BYTES,
        .value.bytes_or_string.data =
                "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
        .value.bytes_or_string.chunk_length = 50
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 5),
        .type = FLUF_DATA_TYPE_BOOL,
        .value.bool_value = false
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 6),
        .type = FLUF_DATA_TYPE_OBJLNK,
        .value.objlnk.oid = 17,
        .value.objlnk.iid = 18,
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 7),
        .type = FLUF_DATA_TYPE_DOUBLE,
        .value.double_value = 1.1
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 8),
        .type = FLUF_DATA_TYPE_EXTERNAL_STRING,
        .value.external_data.length = 30,
        .value.external_data.get_external_data = external_data_handler
    },
    {
        .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 9),
        .type = FLUF_DATA_TYPE_EXTERNAL_BYTES,
        .value.external_data.length = 30,
        .value.external_data.get_external_data = external_data_handler
    }
};
// {8: {8: {
// 0: 25,
// 1: 100,
// 2: 1(3),
// 3: "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
// 4:
// h'4444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444',
// 5: false, 6: "17:18", 7: 1.1,
// 8: "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEE",
// 9: h'454545454545454545454545454545454545454545454545454545454545'
// }}}
static char encoded_entries[] =
        "\xBF\x08\xBF\x08\xBF\x00"
        "\x18\x19"
        "\x01\x18\x64"
        "\x02\xC1\x03"
        "\x03\x78\x32"
        "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
        "\x04\x58\x32"
        "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
        "\x05\xF4"
        "\x06\x65\x31\x37\x3A\x31\x38"
        "\x07\xFB\x3F\xF1\x99\x99\x99\x99\x99\x9A"
        "\x08\x78\x1E"
        "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEE"
        "\x09\x58\x1E"
        "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEE"
        "\xFF\xFF\xFF";

AVS_UNIT_TEST(lwm2m_cbor_encoder, all_data_types_notify_msg) {
    lwm2m_cbor_test_env_t env = { 0 };
    lwm2m_cbor_test_setup(&env, NULL, AVS_ARRAY_SIZE(entries),
                          FLUF_OP_INF_NON_CON_NOTIFY);

    for (size_t i = 0; i < AVS_ARRAY_SIZE(entries); i++) {
        size_t out_len = 0;
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_out_ctx_new_entry(&env.ctx, &entries[i]));
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
                &env.ctx, &env.buf[env.out_length],
                env.buffer_length - env.out_length, &out_len));
        env.out_length += out_len;
    }

    VERIFY_BYTES(env, encoded_entries);
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, all_data_types_chunk_read) {
    lwm2m_cbor_test_env_t env = { 0 };

    for (size_t chunk_len = 16; chunk_len <= sizeof(encoded_entries);
         chunk_len++) {
        lwm2m_cbor_test_setup(&env, &FLUF_MAKE_INSTANCE_PATH(8, 8),
                              AVS_ARRAY_SIZE(entries), FLUF_OP_DM_READ);
        for (size_t i = 0; i < AVS_ARRAY_SIZE(entries); i++) {
            size_t out_len = 0;
            AVS_UNIT_ASSERT_SUCCESS(
                    fluf_io_out_ctx_new_entry(&env.ctx, &entries[i]));
            int res = -1;
            while (res) {
                res = fluf_io_out_ctx_get_payload(&env.ctx,
                                                  &env.buf[env.out_length],
                                                  chunk_len, &out_len);
                AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
                env.out_length += out_len;
            }
        }
        VERIFY_BYTES(env, encoded_entries);
    }
}

AVS_UNIT_TEST(lwm2m_cbor_encoder, errors) {
    lwm2m_cbor_test_env_t env = { 0 };

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };
    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.int_value = 11
    };
    // one entry allowed
    lwm2m_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_CON_SEND);
    size_t out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2),
                          FLUF_IO_ERR_LOGIC);

    // fluf_io_out_ctx_get_payload not called
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2),
                          FLUF_IO_ERR_LOGIC);

    // path outside of the base path
    lwm2m_cbor_test_setup(&env, &FLUF_MAKE_INSTANCE_PATH(8, 8), 1,
                          FLUF_OP_DM_READ);
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1),
                          FLUF_IO_ERR_INPUT_ARG);

    // two identical path
    lwm2m_cbor_test_setup(&env, NULL, 2, FLUF_OP_INF_CON_SEND);
    out_len = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &out_len));
    env.out_length += out_len;
    entry_2.path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3);
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2),
                          FLUF_IO_ERR_INPUT_ARG);
}

#endif // FLUF_WITH_LWM2M_CBOR
