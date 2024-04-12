/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <stddef.h>
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <anj/anj_config.h>
#include <anj/sdm.h>
#include <anj/sdm_io.h>
#include <anj/sdm_server_object.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_utils.h>

#include "../../../src/anj/sdm/sdm_core.h"

#ifdef ANJ_WITH_DEFAULT_SERVER_OBJ

#    define RESOURCE_CHECK_INT(Iid, Rid, Expected_val)                         \
        do {                                                                   \
            fluf_res_value_t val;                                              \
            fluf_uri_path_t path =                                             \
                    FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, Iid, Rid);         \
            AVS_UNIT_ASSERT_SUCCESS(                                           \
                    sdm_operation_begin(&sdm, FLUF_OP_DM_READ, false, &path)); \
            AVS_UNIT_ASSERT_SUCCESS(                                           \
                    _sdm_get_resource_value(&sdm, &path, &val, NULL));         \
            AVS_UNIT_ASSERT_EQUAL(val.int_value, Expected_val);                \
            AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));                  \
        } while (false)

#    define RESOURCE_CHECK_BOOL(Iid, Rid, Expected_val)                        \
        do {                                                                   \
            fluf_res_value_t val;                                              \
            fluf_uri_path_t path =                                             \
                    FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, Iid, Rid);         \
            AVS_UNIT_ASSERT_SUCCESS(                                           \
                    sdm_operation_begin(&sdm, FLUF_OP_DM_READ, false, &path)); \
            AVS_UNIT_ASSERT_SUCCESS(                                           \
                    _sdm_get_resource_value(&sdm, &path, &val, NULL));         \
            AVS_UNIT_ASSERT_EQUAL(val.bool_value, Expected_val);               \
            AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));                  \
        } while (false)

#    define RESOURCE_CHECK_STR(Iid, Rid, Expected_val)                         \
        do {                                                                   \
            fluf_res_value_t val;                                              \
            fluf_uri_path_t path =                                             \
                    FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, Iid, Rid);         \
            AVS_UNIT_ASSERT_SUCCESS(                                           \
                    sdm_operation_begin(&sdm, FLUF_OP_DM_READ, false, &path)); \
            AVS_UNIT_ASSERT_SUCCESS(                                           \
                    _sdm_get_resource_value(&sdm, &path, &val, NULL));         \
            AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data,             \
                                         Expected_val);                        \
            AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));                  \
        } while (false)

#    define INIT_ENV()                                                  \
        sdm_server_obj_t ctx;                                           \
        sdm_data_model_t sdm;                                           \
        sdm_obj_t *objs_array[1];                                       \
        sdm_initialize(&sdm, objs_array, 1);                            \
        sdm_server_obj_init(&ctx);                                      \
        sdm_server_obj_handlers_t handlers = {                          \
            .registration_update_trigger = registration_update_trigger, \
            .bootstrap_request_trigger = bootstrap_request_trigger,     \
        };

static uint16_t last_ssid;
static int registration_update_trigger(uint16_t ssid, void *arg_ptr) {
    (void) ssid;
    (void) arg_ptr;
    last_ssid = ssid + 10;
    return 0;
}
static int bootstrap_request_trigger(uint16_t ssid, void *arg_ptr) {
    (void) ssid;
    (void) arg_ptr;
    last_ssid = ssid + 1000;
    return 0;
}

AVS_UNIT_TEST(sdm_server_object, check_resources_values) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    sdm_server_instance_init_t inst_2 = {
        .ssid = 2,
        .lifetime = 5,
        .default_min_period = 6,
        .default_max_period = 7,
        .binding = "UT",
        .mute_send = true,
        .notification_storing = true
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_SSID, 1);
    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_LIFETIME, 2);
    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_DEFAULT_MIN_PERIOD, 3);
    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_DEFAULT_MAX_PERIOD, 4);
    RESOURCE_CHECK_STR(0, SDM_SERVER_RID_BINDING, "U");
    RESOURCE_CHECK_BOOL(0, SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE,
                        false);
    RESOURCE_CHECK_BOOL(0, SDM_SERVER_RID_MUTE_SEND, false);
    RESOURCE_CHECK_BOOL(
            0, SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            false);

    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_SSID, 2);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_LIFETIME, 5);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_DEFAULT_MIN_PERIOD, 6);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_DEFAULT_MAX_PERIOD, 7);
    RESOURCE_CHECK_STR(1, SDM_SERVER_RID_BINDING, "UT");
    RESOURCE_CHECK_BOOL(1, SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE,
                        true);
    RESOURCE_CHECK_BOOL(1, SDM_SERVER_RID_MUTE_SEND, true);
    RESOURCE_CHECK_BOOL(
            1, SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            true);
}

AVS_UNIT_TEST(sdm_server_object, custom_iid) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .binding = "U",
        .iid = &(fluf_iid_t) { 20 }
    };
    sdm_server_instance_init_t inst_2 = {
        .ssid = 2,
        .lifetime = 5,
        .binding = "UT",
        .iid = &(fluf_iid_t) { 10 }
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    RESOURCE_CHECK_INT(10, SDM_SERVER_RID_SSID, 2);
    RESOURCE_CHECK_INT(20, SDM_SERVER_RID_SSID, 1);
    AVS_UNIT_ASSERT_EQUAL(ctx.inst_ptr[0]->iid, 10);
    AVS_UNIT_ASSERT_EQUAL(ctx.inst_ptr[1]->iid, 20);
}

AVS_UNIT_TEST(sdm_server_object, custom_iid_2) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .binding = "U",
    };
    sdm_server_instance_init_t inst_2 = {
        .ssid = 2,
        .lifetime = 5,
        .binding = "UT",
        .iid = &(fluf_iid_t) { 2 }
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_SSID, 1);
    RESOURCE_CHECK_INT(2, SDM_SERVER_RID_SSID, 2);
    AVS_UNIT_ASSERT_EQUAL(ctx.inst_ptr[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(ctx.inst_ptr[1]->iid, 2);
}

AVS_UNIT_TEST(sdm_server_object, write_replace) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    sdm_server_instance_init_t inst_2 = {
        .ssid = 2,
        .lifetime = 5,
        .default_min_period = 6,
        .default_max_period = 7,
        .binding = "UT",
        .mute_send = true,
        .notification_storing = true
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_REPLACE, true,
                                &FLUF_MAKE_INSTANCE_PATH(SDM_SERVER_OID, 0)));
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_write_entry(&sdm,
                            &(fluf_io_out_entry_t) {
                                .type = FLUF_DATA_TYPE_INT,
                                .value.int_value = 4,
                                .path = FLUF_MAKE_RESOURCE_PATH(
                                        SDM_SERVER_OID, 0, SDM_SERVER_RID_SSID)
                            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 77,
                .path = FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, 0,
                                                SDM_SERVER_RID_LIFETIME)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_STRING,
                .value.bytes_or_string.data = "T",
                .value.bytes_or_string.chunk_length = 1,
                .path = FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, 0,
                                                SDM_SERVER_RID_BINDING)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_SSID, 4);
    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_LIFETIME, 77);
    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_DEFAULT_MIN_PERIOD, 0);
    RESOURCE_CHECK_INT(0, SDM_SERVER_RID_DEFAULT_MAX_PERIOD, 0);
    RESOURCE_CHECK_STR(0, SDM_SERVER_RID_BINDING, "T");
    RESOURCE_CHECK_BOOL(0, SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE,
                        true);
    RESOURCE_CHECK_BOOL(0, SDM_SERVER_RID_MUTE_SEND, false);
    RESOURCE_CHECK_BOOL(
            0, SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            false);

    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_SSID, 2);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_LIFETIME, 5);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_DEFAULT_MIN_PERIOD, 6);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_DEFAULT_MAX_PERIOD, 7);
    RESOURCE_CHECK_STR(1, SDM_SERVER_RID_BINDING, "UT");
    RESOURCE_CHECK_BOOL(1, SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE,
                        true);
    RESOURCE_CHECK_BOOL(1, SDM_SERVER_RID_MUTE_SEND, true);
    RESOURCE_CHECK_BOOL(
            1, SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            true);
}

AVS_UNIT_TEST(sdm_server_object, server_create_instance_minimal) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_CREATE, true,
                                &FLUF_MAKE_OBJECT_PATH(SDM_SERVER_OID)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_create_object_instance(&sdm, 20));
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_write_entry(&sdm,
                            &(fluf_io_out_entry_t) {
                                .type = FLUF_DATA_TYPE_INT,
                                .value.int_value = 7,
                                .path = FLUF_MAKE_RESOURCE_PATH(
                                        SDM_SERVER_OID, 20, SDM_SERVER_RID_SSID)
                            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 8,
                .path = FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, 20,
                                                SDM_SERVER_RID_LIFETIME)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_STRING,
                .value.bytes_or_string.data = "U",
                .value.bytes_or_string.chunk_length = 1,
                .path = FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, 20,
                                                SDM_SERVER_RID_BINDING)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    RESOURCE_CHECK_INT(20, SDM_SERVER_RID_SSID, 7);
    RESOURCE_CHECK_INT(20, SDM_SERVER_RID_LIFETIME, 8);
    RESOURCE_CHECK_INT(20, SDM_SERVER_RID_DEFAULT_MIN_PERIOD, 0);
    RESOURCE_CHECK_INT(20, SDM_SERVER_RID_DEFAULT_MAX_PERIOD, 0);
    RESOURCE_CHECK_STR(20, SDM_SERVER_RID_BINDING, "U");
    RESOURCE_CHECK_BOOL(20, SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE,
                        true);
    RESOURCE_CHECK_BOOL(20, SDM_SERVER_RID_MUTE_SEND, false);
    RESOURCE_CHECK_BOOL(
            20, SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            false);
}

AVS_UNIT_TEST(sdm_server_object, server_create_instance) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_CREATE, true,
                                &FLUF_MAKE_OBJECT_PATH(SDM_SERVER_OID)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_create_object_instance(&sdm, 22));
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_write_entry(&sdm,
                            &(fluf_io_out_entry_t) {
                                .type = FLUF_DATA_TYPE_INT,
                                .value.int_value = 17,
                                .path = FLUF_MAKE_RESOURCE_PATH(
                                        SDM_SERVER_OID, 22, SDM_SERVER_RID_SSID)
                            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 18,
                .path = FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, 22,
                                                SDM_SERVER_RID_LIFETIME)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 19,
                .path = FLUF_MAKE_RESOURCE_PATH(
                        SDM_SERVER_OID, 22, SDM_SERVER_RID_DEFAULT_MIN_PERIOD)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_INT,
                .value.int_value = 20,
                .path = FLUF_MAKE_RESOURCE_PATH(
                        SDM_SERVER_OID, 22, SDM_SERVER_RID_DEFAULT_MAX_PERIOD)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_STRING,
                .value.bytes_or_string.data = "T",
                .value.bytes_or_string.chunk_length = 1,
                .path = FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, 22,
                                                SDM_SERVER_RID_BINDING)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_BOOL,
                .value.bool_value = true,
                .path = FLUF_MAKE_RESOURCE_PATH(SDM_SERVER_OID, 22,
                                                SDM_SERVER_RID_MUTE_SEND)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_BOOL,
                .value.bool_value = true,
                .path = FLUF_MAKE_RESOURCE_PATH(
                        SDM_SERVER_OID, 22,
                        SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(
            &sdm,
            &(fluf_io_out_entry_t) {
                .type = FLUF_DATA_TYPE_BOOL,
                .value.bool_value = false,
                .path = FLUF_MAKE_RESOURCE_PATH(
                        SDM_SERVER_OID, 22,
                        SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE)
            }));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    RESOURCE_CHECK_INT(22, SDM_SERVER_RID_SSID, 17);
    RESOURCE_CHECK_INT(22, SDM_SERVER_RID_LIFETIME, 18);
    RESOURCE_CHECK_INT(22, SDM_SERVER_RID_DEFAULT_MIN_PERIOD, 19);
    RESOURCE_CHECK_INT(22, SDM_SERVER_RID_DEFAULT_MAX_PERIOD, 20);
    RESOURCE_CHECK_STR(22, SDM_SERVER_RID_BINDING, "T");
    RESOURCE_CHECK_BOOL(22, SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE,
                        false);
    RESOURCE_CHECK_BOOL(22, SDM_SERVER_RID_MUTE_SEND, true);
    RESOURCE_CHECK_BOOL(
            22, SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            true);
}

AVS_UNIT_TEST(sdm_server_object, server_create_error) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_CREATE, true,
                                &FLUF_MAKE_OBJECT_PATH(SDM_SERVER_OID)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_create_object_instance(&sdm, 20));
    AVS_UNIT_ASSERT_FAILED(sdm_operation_end(&sdm));
    AVS_UNIT_ASSERT_EQUAL(ctx.obj.inst_count, 1);
}

AVS_UNIT_TEST(sdm_server_object, server_delete_instance) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    sdm_server_instance_init_t inst_2 = {
        .ssid = 2,
        .lifetime = 5,
        .default_min_period = 6,
        .default_max_period = 7,
        .binding = "UT",
        .mute_send = true,
        .notification_storing = true
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_DELETE, false,
                                &FLUF_MAKE_INSTANCE_PATH(SDM_SERVER_OID, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    AVS_UNIT_ASSERT_EQUAL(ctx.obj.inst_count, 1);

    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_SSID, 2);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_LIFETIME, 5);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_DEFAULT_MIN_PERIOD, 6);
    RESOURCE_CHECK_INT(1, SDM_SERVER_RID_DEFAULT_MAX_PERIOD, 7);
    RESOURCE_CHECK_STR(1, SDM_SERVER_RID_BINDING, "UT");
    RESOURCE_CHECK_BOOL(1, SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE,
                        true);
    RESOURCE_CHECK_BOOL(1, SDM_SERVER_RID_MUTE_SEND, true);
    RESOURCE_CHECK_BOOL(
            1, SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            true);

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_DELETE, false,
                                &FLUF_MAKE_INSTANCE_PATH(SDM_SERVER_OID, 1)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    AVS_UNIT_ASSERT_EQUAL(ctx.obj.inst_count, 0);
}

AVS_UNIT_TEST(sdm_server_object, errors) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "UU",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    // UU duplication
    AVS_UNIT_ASSERT_FAILED(sdm_server_obj_add_instance(&ctx, &inst_1));

    sdm_server_instance_init_t inst_2 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_2));
    sdm_server_instance_init_t inst_3 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    // ssid duplication
    AVS_UNIT_ASSERT_FAILED(sdm_server_obj_add_instance(&ctx, &inst_3));
    sdm_server_instance_init_t inst_4 = {
        .ssid = 2,
        .lifetime = 0,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    // incorrect lifetime
    AVS_UNIT_ASSERT_FAILED(sdm_server_obj_add_instance(&ctx, &inst_4));
    sdm_server_instance_init_t inst_5 = {
        .ssid = 2,
        .lifetime = 1,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "B",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    // incorrect binding
    AVS_UNIT_ASSERT_FAILED(sdm_server_obj_add_instance(&ctx, &inst_5));
    sdm_server_instance_init_t inst_6 = {
        .ssid = 2,
        .lifetime = 1,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    // empty binding
    AVS_UNIT_ASSERT_FAILED(sdm_server_obj_add_instance(&ctx, &inst_6));
    sdm_server_instance_init_t inst_7 = {
        .ssid = 1,
        .lifetime = 1,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    // ssid duplication
    AVS_UNIT_ASSERT_FAILED(sdm_server_obj_add_instance(&ctx, &inst_7));
    sdm_server_instance_init_t inst_8 = {
        .ssid = 3,
        .lifetime = 1,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_8));
    sdm_server_instance_init_t inst_9 = {
        .ssid = 4,
        .lifetime = 1,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    // max instances reached
    AVS_UNIT_ASSERT_FAILED(sdm_server_obj_add_instance(&ctx, &inst_9));
}

AVS_UNIT_TEST(sdm_server_object, execute_handlers) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 1,
        .lifetime = 2,
        .default_min_period = 3,
        .default_max_period = 4,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool) { false },
        .mute_send = false,
        .notification_storing = false
    };
    sdm_server_instance_init_t inst_2 = {
        .ssid = 2,
        .lifetime = 5,
        .default_min_period = 6,
        .default_max_period = 7,
        .binding = "UT",
        .mute_send = true,
        .notification_storing = true
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    last_ssid = 0;
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin(
            &sdm, FLUF_OP_DM_EXECUTE, false,
            &FLUF_MAKE_RESOURCE_PATH(
                    SDM_SERVER_OID, 0,
                    SDM_SERVER_RID_REGISTRATION_UPDATE_TRIGGER)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_execute(&sdm, NULL, 0));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    AVS_UNIT_ASSERT_EQUAL(last_ssid, 11);

    last_ssid = 0;
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin(
            &sdm, FLUF_OP_DM_EXECUTE, false,
            &FLUF_MAKE_RESOURCE_PATH(
                    SDM_SERVER_OID, 1,
                    SDM_SERVER_RID_BOOTSTRAP_REQUEST_TRIGGER)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_execute(&sdm, NULL, 0));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    AVS_UNIT_ASSERT_EQUAL(last_ssid, 1002);

    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin(
            &sdm, FLUF_OP_DM_EXECUTE, false,
            &FLUF_MAKE_RESOURCE_PATH(
                    SDM_SERVER_OID, 1,
                    SDM_SERVER_RID_BOOTSTRAP_REQUEST_TRIGGER)));
    ctx.server_obj_handlers.bootstrap_request_trigger = NULL;
    AVS_UNIT_ASSERT_EQUAL(sdm_execute(&sdm, NULL, 0),
                          SDM_ERR_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&sdm), SDM_ERR_METHOD_NOT_ALLOWED);
}

AVS_UNIT_TEST(sdm_server_object, find_instance_iid) {
    INIT_ENV();

    sdm_server_instance_init_t inst_1 = {
        .ssid = 10,
        .lifetime = 2,
        .binding = "U",
    };
    sdm_server_instance_init_t inst_2 = {
        .ssid = 20,
        .lifetime = 5,
        .binding = "UT",
        .iid = &(fluf_iid_t) { 15 }
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_add_instance(&ctx, &inst_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_obj_install(&sdm, &ctx, &handlers));

    fluf_iid_t out_iid;
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_find_instance_iid(&ctx, 10, &out_iid));
    AVS_UNIT_ASSERT_EQUAL(out_iid, 0);
    AVS_UNIT_ASSERT_SUCCESS(sdm_server_find_instance_iid(&ctx, 20, &out_iid));
    AVS_UNIT_ASSERT_EQUAL(out_iid, 15);
    AVS_UNIT_ASSERT_FAILED(sdm_server_find_instance_iid(&ctx, 1, &out_iid));
}

#endif // ANJ_WITH_DEFAULT_SERVER_OBJ
