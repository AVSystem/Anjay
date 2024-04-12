/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifdef FLUF_WITH_SENML_CBOR

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
} senml_cbor_test_env_t;

static void senml_cbor_test_setup(senml_cbor_test_env_t *env,
                                  fluf_uri_path_t *base_path,
                                  size_t items_count,
                                  fluf_op_t op_type) {
    env->buffer_length = sizeof(env->buf);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_init(&env->ctx, op_type, base_path,
                                                 items_count,
                                                 FLUF_COAP_FORMAT_SENML_CBOR));
}

#    define VERIFY_BYTES(Env, Data)                                  \
        do {                                                         \
            AVS_UNIT_ASSERT_EQUAL_BYTES(Env.buf, Data);              \
            AVS_UNIT_ASSERT_EQUAL(Env.out_length, sizeof(Data) - 1); \
        } while (0)

AVS_UNIT_TEST(senml_cbor_encoder, single_send_record_with_all_fields) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry = {
        .timestamp = 100000.0,
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA3"
                      "\x00\x66\x2F\x33\x2F\x33\x2F\x33" // name
                      "\x22\xFA\x47\xC3\x50\x00"         // base time
                      "\x02\x18\x19");
}

AVS_UNIT_TEST(senml_cbor_encoder, single_read_record_with_all_fields) {
    senml_cbor_test_env_t env = { 0 };

    fluf_uri_path_t base_path = FLUF_MAKE_INSTANCE_PATH(3, 3);
    senml_cbor_test_setup(&env, &base_path, 1, FLUF_OP_DM_READ);

    fluf_io_out_entry_t entry = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA3"
                      "\x21\x64\x2F\x33\x2F\x33" // base name
                      "\x00\x62\x2F\x33"         // name
                      "\x02\x18\x19");
}

AVS_UNIT_TEST(senml_cbor_encoder, largest_possible_size_of_single_msg) {
    senml_cbor_test_env_t env = { 0 };

    fluf_uri_path_t base_path = FLUF_MAKE_INSTANCE_PATH(65534, 65534);
    env.buffer_length = sizeof(env.buf);
    env.ctx._format = FLUF_COAP_FORMAT_SENML_CBOR;
    // call _fluf_senml_cbor_encoder_init directly to allow to set basename and
    // timestamp in one message
    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_senml_cbor_encoder_init(&env.ctx, &base_path, 65534, true));

    fluf_io_out_entry_t entry = {
        .timestamp = 1.0e+300,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(65534, 65534, 65534, 65534),
        .type = FLUF_DATA_TYPE_OBJLNK,
        .value.objlnk.oid = 65534,
        .value.objlnk.iid = 65534
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(
            env,
            "\x99\xFF\xFE\xA4"
            "\x21\x6C\x2F\x36\x35\x35\x33\x34\x2F\x36\x35\x35\x33\x34" // basename
            "\x00\x6C\x2F\x36\x35\x35\x33\x34\x2F\x36\x35\x35\x33\x34" // name
            "\x22\xFB\x7E\x37\xE4\x3C\x88\x00\x75\x9C" // base time
            "\x63"
            "vlo" // objlink
            "\x6B\x36\x35\x35\x33\x34\x3A\x36\x35\x35\x33\x34");
    AVS_UNIT_ASSERT_EQUAL(env.out_length,
                          _FLUF_IO_SENML_CBOR_SIMPLE_RECORD_MAX_LENGTH - 1);
}

AVS_UNIT_TEST(senml_cbor_encoder, int) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_NON_CON_NOTIFY);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(77, 77, 77, 77),
        .type = FLUF_DATA_TYPE_INT,
        .value.int_value = -1000
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x6C\x2F\x37\x37\x2F\x37\x37\x2F\x37\x37\x2F\x37\x37"
                      "\x02\x39\x03\xE7");
}

AVS_UNIT_TEST(senml_cbor_encoder, uint) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_NON_CON_NOTIFY);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(77, 77, 77, 77),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = UINT32_MAX
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x6C\x2F\x37\x37\x2F\x37\x37\x2F\x37\x37\x2F\x37\x37"
                      "\x02\x1A\xFF\xFF\xFF\xFF");
}

AVS_UNIT_TEST(senml_cbor_encoder, time) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_NON_CON_NOTIFY);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(77, 77, 77, 77),
        .type = FLUF_DATA_TYPE_TIME,
        .value.time_value = 1000000
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x6C\x2F\x37\x37\x2F\x37\x37\x2F\x37\x37\x2F\x37\x37"
                      "\x02\xC1\x1A\x00\x0F\x42\x40");
}

AVS_UNIT_TEST(senml_cbor_encoder, bool) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_NON_CON_NOTIFY);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_PATH(7, 7, 7),
        .type = FLUF_DATA_TYPE_BOOL,
        .value.bool_value = true
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x66\x2F\x37\x2F\x37\x2F\x37"
                      "\x04\xF5");
}

AVS_UNIT_TEST(senml_cbor_encoder, float) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_NON_CON_NOTIFY);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_PATH(7, 7, 7),
        .type = FLUF_DATA_TYPE_DOUBLE,
        .value.double_value = 100000.0
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x66\x2F\x37\x2F\x37\x2F\x37"
                      "\x02\xFA\x47\xC3\x50\x00");
}

AVS_UNIT_TEST(senml_cbor_encoder, double) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_NON_CON_NOTIFY);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_PATH(7, 7, 7),
        .type = FLUF_DATA_TYPE_DOUBLE,
        .value.double_value = -4.1
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x66\x2F\x37\x2F\x37\x2F\x37"
                      "\x02\xFB\xC0\x10\x66\x66\x66\x66\x66\x66");
}

AVS_UNIT_TEST(senml_cbor_encoder, string) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_NON_CON_NOTIFY);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_PATH(7, 7, 7),
        .type = FLUF_DATA_TYPE_STRING,
        .value.bytes_or_string.data = "DDDDDDDDDD"
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x66\x2F\x37\x2F\x37\x2F\x37"
                      "\x03\x6A"
                      "DDDDDDDDDD");
}

AVS_UNIT_TEST(senml_cbor_encoder, bytes) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_NON_CON_NOTIFY);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_PATH(7, 7, 7),
        .type = FLUF_DATA_TYPE_BYTES,
        .value.bytes_or_string.data = "DDDDDDDDDD",
        .value.bytes_or_string.chunk_length = 10
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x66\x2F\x37\x2F\x37\x2F\x37"
                      "\x08\x4A"
                      "DDDDDDDDDD");
}

static char *ptr_for_callback = NULL;
static int external_data_handler(void *buffer,
                                 size_t bytes_to_copy,
                                 size_t offset,
                                 void *args) {
    (void) args;
    memcpy(buffer, &ptr_for_callback[offset], bytes_to_copy);
    return 0;
}

AVS_UNIT_TEST(senml_cbor_encoder, ext_string) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_PATH(7, 7, 7),
        .type = FLUF_DATA_TYPE_EXTERNAL_STRING,
        .value.external_data.length = 10,
        .value.external_data.user_args = NULL,
        .value.external_data.get_external_data = external_data_handler
    };
    ptr_for_callback = "DDDDDDDDDD";

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x66\x2F\x37\x2F\x37\x2F\x37"
                      "\x03\x6A"
                      "DDDDDDDDDD");
}

AVS_UNIT_TEST(senml_cbor_encoder, ext_bytes) {
    senml_cbor_test_env_t env = { 0 };

    senml_cbor_test_setup(&env, NULL, 1, FLUF_OP_INF_CON_SEND);

    fluf_io_out_entry_t entry = {
        .timestamp = NAN,
        .path = FLUF_MAKE_RESOURCE_PATH(7, 7, 7),
        .type = FLUF_DATA_TYPE_EXTERNAL_BYTES,
        .value.external_data.length = 10,
        .value.external_data.user_args = NULL,
        .value.external_data.get_external_data = external_data_handler
    };
    ptr_for_callback = "DDDDDDDDDD";

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    VERIFY_BYTES(env, "\x81\xA2"
                      "\x00\x66\x2F\x37\x2F\x37\x2F\x37"
                      "\x08\x4A"
                      "DDDDDDDDDD");
}

AVS_UNIT_TEST(senml_cbor_encoder, complex_notify_msg) {
    senml_cbor_test_env_t env = { 0 };
    fluf_io_out_entry_t entries[] = {
        {
            .timestamp = 65504.0,
            .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 0),
            .type = FLUF_DATA_TYPE_INT,
            .value.int_value = 25,
        },
        {
            .timestamp = 65504.0,
            .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 1),
            .type = FLUF_DATA_TYPE_UINT,
            .value.uint_value = 100,
        },
        {
            .timestamp = 65504.0,
            .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 2),
            .type = FLUF_DATA_TYPE_STRING,
            .value.bytes_or_string.data =
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
        },
        {
            .timestamp = 65504.0,
            .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 1),
            .type = FLUF_DATA_TYPE_BYTES,
            .value.bytes_or_string.data =
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
            .value.bytes_or_string.chunk_length = 200
        },
        {
            .timestamp = 1.5,
            .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 25),
            .type = FLUF_DATA_TYPE_BOOL,
            .value.bool_value = false
        },
        {
            .timestamp = 1.5,
            .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 26),
            .type = FLUF_DATA_TYPE_OBJLNK,
            .value.objlnk.oid = 17,
            .value.objlnk.iid = 19,
        }
    };

    senml_cbor_test_setup(&env, NULL, AVS_ARRAY_SIZE(entries),
                          FLUF_OP_INF_NON_CON_NOTIFY);

    for (size_t i = 0; i < AVS_ARRAY_SIZE(entries); i++) {
        size_t record_len = 0;

        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_out_ctx_new_entry(&env.ctx, &entries[i]));
        int res = -1;
        while (res) {
            size_t temp_len = 0;
            res = fluf_io_out_ctx_get_payload(
                    &env.ctx, &env.buf[env.out_length + record_len], 50,
                    &temp_len);
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
            record_len += temp_len;
        }
        env.out_length += record_len;
    }

    VERIFY_BYTES(env, "\x86\xA3"
                      "\x00\x66\x2F\x38\x2F\x38\x2F\x30" // 8/8/0
                      "\x22\xFA\x47\x7F\xE0\x00"         // base time
                      "\x02\x18\x19"
                      "\xA2" // 8/8/1
                      "\x00\x66\x2F\x38\x2F\x38\x2F\x31"
                      "\x02\x18\x64"
                      "\xA2" // 8/8/2
                      "\x00\x66\x2F\x38\x2F\x38\x2F\x32"
                      "\x03\x78\x64"
                      "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                      "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                      "\xA2" // 1/1/1
                      "\x00\x66\x2F\x31\x2F\x31\x2F\x31"
                      "\x08\x58\xC8"
                      "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                      "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                      "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                      "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                      "\xA3" // 1/1/25
                      "\x00\x67\x2F\x31\x2F\x31\x2F\x32\x35"
                      "\x22\xFA\x3F\xC0\x00\x00"
                      "\x04\xF4"
                      "\xA2" // 1/1/26
                      "\x00\x67\x2F\x31\x2F\x31\x2F\x32\x36"
                      "\x63"
                      "vlo"
                      "\x65\x31\x37\x3A\x31\x39");
}

AVS_UNIT_TEST(senml_cbor_encoder, complex_read_msg) {
    senml_cbor_test_env_t env = { 0 };
    fluf_uri_path_t base_path = FLUF_MAKE_INSTANCE_PATH(8, 8);
    fluf_io_out_entry_t entries[] = {
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
            .type = FLUF_DATA_TYPE_STRING,
            .value.bytes_or_string.data =
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
        },
        {
            .path = FLUF_MAKE_RESOURCE_PATH(8, 8, 3),
            .type = FLUF_DATA_TYPE_BYTES,
            .value.bytes_or_string.data =
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
            .value.bytes_or_string.chunk_length = 200
        },
        {
            .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(8, 8, 4, 0),
            .type = FLUF_DATA_TYPE_BOOL,
            .value.bool_value = false
        },
        {
            .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(8, 8, 4, 1),
            .type = FLUF_DATA_TYPE_OBJLNK,
            .value.objlnk.oid = 17,
            .value.objlnk.iid = 19,
        }
    };

    for (size_t chunk_len = 50; chunk_len < 370; chunk_len += 10) {
        env.out_length = 0;
        senml_cbor_test_setup(&env, &base_path, AVS_ARRAY_SIZE(entries),
                              FLUF_OP_DM_READ);

        for (size_t i = 0; i < AVS_ARRAY_SIZE(entries); i++) {
            size_t record_len = 0;
            AVS_UNIT_ASSERT_SUCCESS(
                    fluf_io_out_ctx_new_entry(&env.ctx, &entries[i]));
            int res = -1;
            while (res) {
                size_t temp_len = 0;
                res = fluf_io_out_ctx_get_payload(
                        &env.ctx, &env.buf[env.out_length + record_len],
                        chunk_len, &temp_len);
                AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
                record_len += temp_len;
            }
            env.out_length += record_len;
        }

        VERIFY_BYTES(env,
                     "\x86\xA3"
                     "\x21\x64\x2F\x38\x2F\x38"
                     "\x00\x62\x2F\x30" // 8/8/0
                     "\x02\x18\x19"
                     "\xA2" // 8/8/1
                     "\x00\x62\x2F\x31"
                     "\x02\x18\x64"
                     "\xA2" // 8/8/2
                     "\x00\x62\x2F\x32"
                     "\x03\x78\x64"
                     "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                     "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                     "\xA2" // 8/8/3
                     "\x00\x62\x2F\x33"
                     "\x08\x58\xC8"
                     "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                     "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                     "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                     "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
                     "\xA2" // 8/8/4/0
                     "\x00\x64\x2F\x34\x2F\x30"
                     "\x04\xF4"
                     "\xA2" // 8/8/4/1
                     "\x00\x64\x2F\x34\x2F\x31"
                     "\x63"
                     "vlo"
                     "\x65\x31\x37\x3A\x31\x39");
    }
}

#    define DATA_HANDLER_ERROR_CODE -888
static int external_data_handler_with_error(void *buffer,
                                            size_t bytes_to_copy,
                                            size_t offset,
                                            void *args) {
    (void) buffer;
    (void) bytes_to_copy;
    (void) offset;
    (void) args;
    return DATA_HANDLER_ERROR_CODE;
}

AVS_UNIT_TEST(senml_cbor_encoder, read_error) {
    senml_cbor_test_env_t env = { 0 };

    fluf_uri_path_t base_path = FLUF_MAKE_INSTANCE_PATH(3, 3);
    senml_cbor_test_setup(&env, &base_path, 1, FLUF_OP_DM_READ);

    fluf_io_out_entry_t entry_1 = {
        .path = FLUF_MAKE_RESOURCE_PATH(1, 3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    AVS_UNIT_ASSERT_FAILED(fluf_io_out_ctx_new_entry(&env.ctx, &entry_1));

    fluf_io_out_entry_t entry_2 = {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 1, 3, 1),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    AVS_UNIT_ASSERT_FAILED(fluf_io_out_ctx_new_entry(&env.ctx, &entry_2));

    fluf_io_out_entry_t entry_3 = {
        .path = FLUF_MAKE_INSTANCE_PATH(3, 3),
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = 25
    };

    AVS_UNIT_ASSERT_FAILED(fluf_io_out_ctx_new_entry(&env.ctx, &entry_3));

    fluf_io_out_entry_t entry_4 = {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 3, 4),
        .type = FLUF_DATA_TYPE_EXTERNAL_STRING,
        .value.external_data.length = 10,
        .value.external_data.user_args = NULL,
        .value.external_data.get_external_data =
                external_data_handler_with_error
    };

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &entry_4));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_payload(&env.ctx, env.buf,
                                                      env.buffer_length,
                                                      &env.out_length),
                          DATA_HANDLER_ERROR_CODE);
}
#endif // FLUF_WITH_SENML_CBOR
