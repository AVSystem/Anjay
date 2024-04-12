/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <stdbool.h>

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/anj_config.h>
#include <anj/sdm.h>
#include <anj/sdm_io.h>
#include <anj/sdm_security_object.h>

#ifdef ANJ_WITH_DEFAULT_SECURITY_OBJ

#    define RESOURCE_CHECK_INT(Iid, SecInstElement, ExpectedValue)            \
        for (int i = 0; i < ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER; i++) { \
            if (ctx.inst[i].iid == Iid) {                                     \
                AVS_UNIT_ASSERT_EQUAL(SecInstElement, ExpectedValue);         \
            }                                                                 \
        }

#    define RESOURCE_CHECK_BYTES(Iid, SecInstElement, ExpectedValue,          \
                                 ExpectedValueLen)                            \
        for (int i = 0; i < ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER; i++) { \
            if (ctx.inst[i].iid == Iid) {                                     \
                AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(                            \
                        SecInstElement, ExpectedValue, ExpectedValueLen);     \
            }                                                                 \
        }

#    define RESOURCE_CHECK_STRING(Iid, SecInstElement, ExpectedValue) \
        RESOURCE_CHECK_BYTES(Iid, SecInstElement, ExpectedValue,      \
                             sizeof(ExpectedValue) - 1)

#    define RESOURCE_CHECK_BOOL(Iid, SecInstElement, ExpectedValue) \
        RESOURCE_CHECK_INT(Iid, SecInstElement, ExpectedValue)

#    define INIT_ENV()                       \
        sdm_security_obj_t ctx;              \
        sdm_data_model_t sdm;                \
        sdm_obj_t *objs_array[1];            \
        sdm_initialize(&sdm, objs_array, 1); \
        sdm_security_obj_init(&ctx);

#    define PUBLIC_KEY_OR_IDENTITY_1 "public_key"
#    define SERVER_PUBLIC_KEY_1 \
        "server"                \
        "\x00\x01"              \
        "key"
#    define SECRET_KEY_1 "\x55\x66\x77\x88"

#    define PUBLIC_KEY_OR_IDENTITY_2 "advanced_public_key"
#    define SERVER_PUBLIC_KEY_2 \
        "server"                \
        "\x00\x02\x03"          \
        "key"
#    define SECRET_KEY_2 "\x99\x88\x77\x66\x55"

AVS_UNIT_TEST(sdm_security_object, check_resources_values) {
    INIT_ENV();

    sdm_security_instance_init_t inst_1 = {
        .server_uri = "coap://server.com:5683",
        .bootstrap_server = true,
        .security_mode = 1,
        .public_key_or_identity = PUBLIC_KEY_OR_IDENTITY_1,
        .public_key_or_identity_size = sizeof(PUBLIC_KEY_OR_IDENTITY_1) - 1,
        .server_public_key = SERVER_PUBLIC_KEY_1,
        .server_public_key_size = sizeof(SERVER_PUBLIC_KEY_1) - 1,
        .secret_key = SECRET_KEY_1,
        .secret_key_size = sizeof(SECRET_KEY_1) - 1,
        .ssid = 1,
    };
    sdm_security_instance_init_t inst_2 = {
        .server_uri = "coaps://server.com:5684",
        .bootstrap_server = false,
        .security_mode = 2,
        .public_key_or_identity = PUBLIC_KEY_OR_IDENTITY_2,
        .public_key_or_identity_size = sizeof(PUBLIC_KEY_OR_IDENTITY_2) - 1,
        .server_public_key = SERVER_PUBLIC_KEY_2,
        .server_public_key_size = sizeof(SERVER_PUBLIC_KEY_2) - 1,
        .secret_key = SECRET_KEY_2,
        .secret_key_size = sizeof(SECRET_KEY_2) - 1,
        .ssid = 2,
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_install(&sdm, &ctx));

    RESOURCE_CHECK_STRING(0, ctx.security_instances[0].server_uri,
                          "coap://server.com:5683");
    RESOURCE_CHECK_BOOL(0, ctx.security_instances[0].bootstrap_server, true);
    RESOURCE_CHECK_INT(0, ctx.security_instances[0].security_mode, 1);
    RESOURCE_CHECK_BYTES(0,
                         ctx.security_instances[0].public_key_or_identity,
                         PUBLIC_KEY_OR_IDENTITY_1,
                         sizeof(PUBLIC_KEY_OR_IDENTITY_1) - 1);
    RESOURCE_CHECK_BYTES(0, ctx.security_instances[0].server_public_key,
                         SERVER_PUBLIC_KEY_1, sizeof(SERVER_PUBLIC_KEY_1) - 1);
    RESOURCE_CHECK_BYTES(0, ctx.security_instances[0].secret_key, SECRET_KEY_1,
                         sizeof(SECRET_KEY_1) - 1);
    RESOURCE_CHECK_INT(0, ctx.security_instances[0].ssid, 1);
    RESOURCE_CHECK_STRING(1, ctx.security_instances[i].server_uri,
                          "coaps://server.com:5684");
    RESOURCE_CHECK_BOOL(1, ctx.security_instances[i].bootstrap_server, false);
    RESOURCE_CHECK_INT(1, ctx.security_instances[i].security_mode, 2);
    RESOURCE_CHECK_BYTES(1,
                         ctx.security_instances[i].public_key_or_identity,
                         PUBLIC_KEY_OR_IDENTITY_2,
                         sizeof(PUBLIC_KEY_OR_IDENTITY_2) - 1);
    RESOURCE_CHECK_BYTES(1, ctx.security_instances[i].server_public_key,
                         SERVER_PUBLIC_KEY_2, sizeof(SERVER_PUBLIC_KEY_2) - 1);
    RESOURCE_CHECK_BYTES(1, ctx.security_instances[i].secret_key, SECRET_KEY_2,
                         sizeof(SECRET_KEY_2) - 1);
    RESOURCE_CHECK_INT(1, ctx.security_instances[i].ssid, 2);
}

AVS_UNIT_TEST(sdm_security_object, create_instance_minimal) {
    INIT_ENV();

    sdm_security_instance_init_t inst_1 = {
        .server_uri = "coap://server.com:5683",
        .ssid = 1,
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_install(&sdm, &ctx));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_CREATE, true,
                                &FLUF_MAKE_OBJECT_PATH(FLUF_OBJ_ID_SECURITY)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_create_object_instance(&sdm, 20));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_STRING,
                .value.bytes_or_string.data = "coap://test.com:5684",
                .value.bytes_or_string.chunk_length =
                        sizeof("coap://test.com:5684") - 1,
                .path = FLUF_MAKE_RESOURCE_PATH(FLUF_OBJ_ID_SECURITY, 20,
                                                SDM_SECURITY_RID_SERVER_URI)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 7,
                .path = FLUF_MAKE_RESOURCE_PATH(FLUF_OBJ_ID_SECURITY, 20,
                                                SDM_SECURITY_RID_SSID)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    RESOURCE_CHECK_STRING(0, ctx.security_instances[0].server_uri,
                          "coap://server.com:5683");
    RESOURCE_CHECK_INT(0, ctx.security_instances[0].ssid, 1);
    RESOURCE_CHECK_STRING(20, ctx.security_instances[i].server_uri,
                          "coap://test.com:5684");
    RESOURCE_CHECK_BOOL(20, ctx.security_instances[i].bootstrap_server, false);
    RESOURCE_CHECK_INT(20, ctx.security_instances[i].security_mode, 0);
    RESOURCE_CHECK_BYTES(20, ctx.security_instances[i].public_key_or_identity,
                         "", 0);
    RESOURCE_CHECK_INT(20, ctx.security_instances[i].ssid, 7);
}

AVS_UNIT_TEST(sdm_security_object, create_instance) {
    INIT_ENV();

    sdm_security_instance_init_t inst_1 = {
        .server_uri = "coap://server.com:5683",
        .ssid = 1,
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_install(&sdm, &ctx));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_CREATE, true,
                                &FLUF_MAKE_OBJECT_PATH(FLUF_OBJ_ID_SECURITY)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_create_object_instance(&sdm, 20));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_STRING,
                .value.bytes_or_string.data = "coap://test.com:5683",
                .value.bytes_or_string.chunk_length =
                        sizeof("coap://test.com:5683") - 1,
                .path = FLUF_MAKE_RESOURCE_PATH(FLUF_OBJ_ID_SECURITY, 20,
                                                SDM_SECURITY_RID_SERVER_URI)
            }));
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_write_entry(&sdm,
                            &(fluf_io_out_entry_t) {
                                .type = FLUF_DATA_TYPE_BOOL,
                                .value.bool_value = true,
                                .path = FLUF_MAKE_RESOURCE_PATH(
                                        FLUF_OBJ_ID_SECURITY, 20,
                                        SDM_SECURITY_RID_BOOTSTRAP_SERVER)
                            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 1,
                .path = FLUF_MAKE_RESOURCE_PATH(FLUF_OBJ_ID_SECURITY, 20,
                                                SDM_SECURITY_RID_SECURITY_MODE)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_BYTES,
                .value.bytes_or_string.data = PUBLIC_KEY_OR_IDENTITY_1,
                .value.bytes_or_string.chunk_length =
                        sizeof(PUBLIC_KEY_OR_IDENTITY_1) - 1,
                .value.bytes_or_string.full_length_hint =
                        sizeof(PUBLIC_KEY_OR_IDENTITY_1) - 1,
                .path = FLUF_MAKE_RESOURCE_PATH(
                        FLUF_OBJ_ID_SECURITY, 20,
                        SDM_SECURITY_RID_PUBLIC_KEY_OR_IDENTITY)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_BYTES,
                .value.bytes_or_string.data = SERVER_PUBLIC_KEY_1,
                .value.bytes_or_string.chunk_length =
                        sizeof(SERVER_PUBLIC_KEY_1) - 1,
                .value.bytes_or_string.full_length_hint =
                        sizeof(SERVER_PUBLIC_KEY_1) - 1,
                .path = FLUF_MAKE_RESOURCE_PATH(
                        FLUF_OBJ_ID_SECURITY, 20,
                        SDM_SECURITY_RID_SERVER_PUBLIC_KEY)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_BYTES,
                .value.bytes_or_string.data = SECRET_KEY_1,
                .value.bytes_or_string.chunk_length = sizeof(SECRET_KEY_1) - 1,
                .value.bytes_or_string.full_length_hint =
                        sizeof(SECRET_KEY_1) - 1,
                .path = FLUF_MAKE_RESOURCE_PATH(FLUF_OBJ_ID_SECURITY, 20,
                                                SDM_SECURITY_RID_SECRET_KEY)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 7,
                .path = FLUF_MAKE_RESOURCE_PATH(FLUF_OBJ_ID_SECURITY, 20,
                                                SDM_SECURITY_RID_SSID)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    RESOURCE_CHECK_STRING(0, ctx.security_instances[0].server_uri,
                          "coap://server.com:5683");
    RESOURCE_CHECK_INT(0, ctx.security_instances[0].ssid, 1);
    RESOURCE_CHECK_STRING(20, ctx.security_instances[i].server_uri,
                          "coap://test.com:5683");
    RESOURCE_CHECK_BOOL(20, ctx.security_instances[i].bootstrap_server, true);
    RESOURCE_CHECK_INT(20, ctx.security_instances[i].security_mode, 1);
    RESOURCE_CHECK_BYTES(20,
                         ctx.security_instances[i].public_key_or_identity,
                         PUBLIC_KEY_OR_IDENTITY_1,
                         sizeof(PUBLIC_KEY_OR_IDENTITY_1) - 1);
    RESOURCE_CHECK_BYTES(20, ctx.security_instances[i].server_public_key,
                         SERVER_PUBLIC_KEY_1, sizeof(SERVER_PUBLIC_KEY_1) - 1);
    RESOURCE_CHECK_BYTES(20, ctx.security_instances[i].secret_key, SECRET_KEY_1,
                         sizeof(SECRET_KEY_1) - 1);
    RESOURCE_CHECK_INT(20, ctx.security_instances[i].ssid, 7);
}

AVS_UNIT_TEST(sdm_security_object, delete_instance) {
    INIT_ENV();

    sdm_security_instance_init_t inst_1 = {
        .server_uri = "coap://server.com:5683",
        .ssid = 1,
    };
    sdm_security_instance_init_t inst_2 = {
        .server_uri = "coaps://server.com:5684",
        .ssid = 2,
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_install(&sdm, &ctx));

    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin(
            &sdm, FLUF_OP_DM_DELETE, true,
            &FLUF_MAKE_INSTANCE_PATH(FLUF_OBJ_ID_SECURITY, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    AVS_UNIT_ASSERT_EQUAL(ctx.obj.inst_count, 1);

    RESOURCE_CHECK_STRING(1, ctx.security_instances[i].server_uri,
                          "coaps://server.com:5684");
    RESOURCE_CHECK_INT(1, ctx.security_instances[i].ssid, 2);

    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin(
            &sdm, FLUF_OP_DM_DELETE, true,
            &FLUF_MAKE_INSTANCE_PATH(FLUF_OBJ_ID_SECURITY, 1)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    AVS_UNIT_ASSERT_EQUAL(ctx.obj.inst_count, 0);
}

AVS_UNIT_TEST(sdm_security_object, errors) {
    INIT_ENV();

    sdm_security_instance_init_t inst_1 = {
        .server_uri = "coap://server.com:5683",
        .ssid = 1,
    };
    sdm_security_instance_init_t inst_2 = {
        .server_uri = "coaps://server.com:5684",
        .ssid = 1,
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_add_instance(&ctx, &inst_1));
    // ssid duplication
    AVS_UNIT_ASSERT_FAILED(sdm_security_obj_add_instance(&ctx, &inst_2));

    sdm_security_instance_init_t inst_3 = {
        .server_uri = "coap://server.com:5683",
        .ssid = 2,
    };
    // server_uri duplication
    AVS_UNIT_ASSERT_FAILED(sdm_security_obj_add_instance(&ctx, &inst_3));

    sdm_security_instance_init_t inst_4 = {
        .server_uri = "coap://test.com:5683",
        .ssid = 2,
        .security_mode = 5
    };
    // invalid security mode
    AVS_UNIT_ASSERT_FAILED(sdm_security_obj_add_instance(&ctx, &inst_4));

    sdm_security_instance_init_t inst_5 = {
        .server_uri = "coap://test.com:5683",
        .ssid = 2,
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_add_instance(&ctx, &inst_5));

    sdm_security_instance_init_t inst_6 = {
        .server_uri = "coap://test.com:5684",
        .ssid = 3,
    };
    // max instances reached
    AVS_UNIT_ASSERT_FAILED(sdm_security_obj_add_instance(&ctx, &inst_6));

    AVS_UNIT_ASSERT_SUCCESS(sdm_security_obj_install(&sdm, &ctx));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin(
            &sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, true,
            &FLUF_MAKE_RESOURCE_PATH(FLUF_OBJ_ID_SECURITY, 0, 2)));
    AVS_UNIT_ASSERT_FAILED(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 5,
                .path = FLUF_MAKE_RESOURCE_PATH(FLUF_OBJ_ID_SECURITY, 0,
                                                SDM_SECURITY_RID_SECURITY_MODE)
            }));
    int ret = sdm_operation_end(&sdm);
    AVS_UNIT_ASSERT_EQUAL(ret, SDM_ERR_BAD_REQUEST);
}

#endif // ANJ_WITH_DEFAULT_SECURITY_OBJ
