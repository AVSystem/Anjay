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
#include <anj/sdm_fw_update.h>
#include <anj/sdm_io.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_utils.h>

#include "../../../src/anj/sdm/sdm_core.h"

#ifdef ANJ_WITH_FOTA_OBJECT

#    define EXAMPLE_URI "coap://eu.iot.avsystem.cloud:5663"

#    define BEGIN_READ                               \
        do {                                         \
        AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin( \
                &sdm, FLUF_OP_DM_READ, false, &FLUF_MAKE_OBJECT_PATH(5)))

#    define END_READ                                      \
        AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm)); \
        }                                                 \
        while (false)

#    define SUCCESS 1
#    define FAIL 0

#    define PERFORM_RESOURCE_READ(Rid, result)                           \
        do {                                                             \
            if (result) {                                                \
                AVS_UNIT_ASSERT_SUCCESS(_sdm_get_resource_value(         \
                        &sdm, &FLUF_MAKE_RESOURCE_PATH(5, 0, Rid), &val, \
                        &out_type));                                     \
            } else {                                                     \
                AVS_UNIT_ASSERT_FAILED(_sdm_get_resource_value(          \
                        &sdm, &FLUF_MAKE_RESOURCE_PATH(5, 0, Rid), &val, \
                        &out_type));                                     \
            }                                                            \
        } while (false)

#    define PERFORM_RESOURCE_INSTANCE_READ(Rid, Riid, result)               \
        do {                                                                \
            if (result) {                                                   \
                AVS_UNIT_ASSERT_SUCCESS(_sdm_get_resource_value(            \
                        &sdm,                                               \
                        &FLUF_MAKE_RESOURCE_INSTANCE_PATH(5, 0, Rid, Riid), \
                        &val, &out_type));                                  \
            } else {                                                        \
                AVS_UNIT_ASSERT_FAILED(_sdm_get_resource_value(             \
                        &sdm,                                               \
                        &FLUF_MAKE_RESOURCE_INSTANCE_PATH(5, 0, Rid, Riid), \
                        &val, &out_type));                                  \
            }                                                               \
        } while (false)

typedef struct {
    char order[10];
    bool fail;
} arg_t;

#    define INIT_ENV_SDM(handlers)                            \
        sdm_fw_update_entity_ctx_t entity_ctx;                \
        arg_t user_arg = { 0 };                               \
        sdm_data_model_t sdm;                                 \
        sdm_obj_t *objs_array[2];                             \
        sdm_initialize(&sdm, objs_array, 2);                  \
        AVS_UNIT_ASSERT_SUCCESS(sdm_fw_update_object_install( \
                &sdm, &entity_ctx, &(handlers), &user_arg));  \
        AVS_UNIT_ASSERT_EQUAL(sdm.objs_count, 1);             \
        fluf_res_value_t val;                                 \
        fluf_data_type_t out_type

static const char pkg_name[] = "sdm_test_name";
static const char pkg_ver[] = "sdm_test_ver";
static uint8_t package_buffer[512];
static size_t package_buffer_offset;
static sdm_fw_update_result_t result_to_return;
static char expected_uri[256];

static sdm_fw_update_result_t user_package_write_start_handler(void *user_ptr) {
    arg_t *arg = (arg_t *) user_ptr;
    strcat(arg->order, "0");
    return result_to_return;
}

static sdm_fw_update_result_t
user_package_write_handler(void *user_ptr, void *data, size_t data_size) {
    arg_t *arg = (arg_t *) user_ptr;
    strcat(arg->order, "1");
    memcpy(&package_buffer[package_buffer_offset], data, data_size);
    package_buffer_offset += data_size;
    return result_to_return;
}

static sdm_fw_update_result_t
user_package_write_finish_handler(void *user_ptr) {
    arg_t *arg = (arg_t *) user_ptr;
    strcat(arg->order, "2");
    return result_to_return;
}

static sdm_fw_update_result_t user_uri_write_handler(void *user_ptr,
                                                     const char *uri) {
    arg_t *arg = (arg_t *) user_ptr;
    strcat(arg->order, "3");
    AVS_UNIT_ASSERT_EQUAL_STRING(uri, expected_uri);
    return result_to_return;
}

static int user_update_start_handler(void *user_ptr) {
    arg_t *arg = (arg_t *) user_ptr;
    strcat(arg->order, "4");
    if (arg->fail) {
        return 1;
    } else {
        return 0;
    }
}

static const char *user_get_name(void *user_ptr) {
    arg_t *arg = (arg_t *) user_ptr;
    strcat(arg->order, "5");
    if (arg->fail) {
        return NULL;
    } else {
        return pkg_name;
    }
}

static const char *user_get_ver(void *user_ptr) {
    arg_t *arg = (arg_t *) user_ptr;
    strcat(arg->order, "6");
    if (arg->fail) {
        return NULL;
    } else {
        return pkg_ver;
    }
}

static void user_reset_handler(void *user_ptr) {
    arg_t *arg = (arg_t *) user_ptr;
    strcat(arg->order, "7");
}

static sdm_fw_update_handlers_t handlers = {
    .package_write_start_handler = &user_package_write_start_handler,
    .package_write_handler = &user_package_write_handler,
    .package_write_finish_handler = &user_package_write_finish_handler,
    .uri_write_handler = &user_uri_write_handler,
    .update_start_handler = &user_update_start_handler,
    .get_name = &user_get_name,
    .get_version = &user_get_ver,
    .reset_handler = &user_reset_handler
};

AVS_UNIT_TEST(sdm_fw_update, reading_resources) {
    INIT_ENV_SDM(handlers);

    BEGIN_READ;
    // Package
    PERFORM_RESOURCE_READ(0, FAIL);
    // URI
    PERFORM_RESOURCE_READ(1, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, "");
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_STRING);
    // update
    PERFORM_RESOURCE_READ(2, FAIL);
    // state
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    // no resource
    PERFORM_RESOURCE_READ(4, FAIL);
    // result
    PERFORM_RESOURCE_READ(5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_RESULT_INITIAL);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    // pkg name
    PERFORM_RESOURCE_READ(6, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, pkg_name);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_STRING);
    // pkg version
    PERFORM_RESOURCE_READ(7, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, pkg_ver);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_STRING);
    // protocol support
    PERFORM_RESOURCE_INSTANCE_READ(8, 0, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 0);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    PERFORM_RESOURCE_INSTANCE_READ(8, 1, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 1);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    PERFORM_RESOURCE_INSTANCE_READ(8, 2, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 2);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    PERFORM_RESOURCE_INSTANCE_READ(8, 3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 3);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    PERFORM_RESOURCE_INSTANCE_READ(8, 4, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 4);
    PERFORM_RESOURCE_INSTANCE_READ(8, 5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 5);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    PERFORM_RESOURCE_READ(9, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 2);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    END_READ;

    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "56");
}

static sdm_fw_update_handlers_t handlers_simple = {
    .package_write_start_handler = &user_package_write_start_handler,
    .package_write_handler = &user_package_write_handler,
    .package_write_finish_handler = &user_package_write_finish_handler,
    .uri_write_handler = &user_uri_write_handler,
    .update_start_handler = &user_update_start_handler,
    .reset_handler = &user_reset_handler
};

AVS_UNIT_TEST(sdm_fw_update, simple_handlers) {
    INIT_ENV_SDM(handlers_simple);

    BEGIN_READ;
    PERFORM_RESOURCE_READ(6, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, "");
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_STRING);

    PERFORM_RESOURCE_READ(7, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, "");
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_STRING);
    END_READ;
}

AVS_UNIT_TEST(sdm_fw_update, null_pkg_metadata) {
    INIT_ENV_SDM(handlers_simple);

    user_arg.fail = true;
    BEGIN_READ;
    PERFORM_RESOURCE_READ(6, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, "");
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_STRING);

    PERFORM_RESOURCE_READ(7, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, "");
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_STRING);
    END_READ;
}

#    if defined(ANJ_FOTA_PUSH_METHOD_SUPPORTED) \
            && !defined(ANJ_FOTA_PULL_METHOD_SUPPORTED)
static sdm_fw_update_handlers_t handlers_simple_push = {
    .package_write_start_handler = &user_package_write_start_handler,
    .package_write_handler = &user_package_write_handler,
    .package_write_finish_handler = &user_package_write_finish_handler,
    .update_start_handler = &user_update_start_handler,
    .reset_handler = &user_reset_handler
};

AVS_UNIT_TEST(sdm_fw_update, simple_handlers_push_only) {
    INIT_ENV_SDM(handlers_simple_push);

    // reset the testing buffer
    memset(package_buffer, '\0', sizeof(package_buffer));
    package_buffer_offset = 0;

    // write partial data
    uint8_t data[256] = { 1 };
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_BYTES,
        .value.bytes_or_string.data = data,
        .value.bytes_or_string.chunk_length = 250,
        .value.bytes_or_string.full_length_hint = 256,
        .path = FLUF_MAKE_RESOURCE_PATH(5, 0, 0)
    };
    result_to_return = SDM_FW_UPDATE_RESULT_SUCCESS;

    // can't set the result in idle state
    AVS_UNIT_ASSERT_FAILED(sdm_fw_update_object_set_download_result(0));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    // write start and write
    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "01");

    // check if state == idle
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    END_READ;

    // write the rest
    record.value.bytes_or_string.chunk_length = 6;
    record.value.bytes_or_string.offset = 250;
    err_to_return = SDM_FW_UPDATE_RESULT_SUCCESS;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    // write start, 2 writes and write finish
    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "0112");

    // check if state == DOWNLOADED and result applied
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_DOWNLOADED);
    PERFORM_RESOURCE_READ(5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_RESULT_SUCCESS);

    PERFORM_RESOURCE_READ(9, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 1);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    END_READ;

    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "56");
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(package_buffer, data, 256);
}
#    endif // defined(ANJ_FOTA_PUSH_METHOD_SUPPORTED) &&
           // !defined(ANJ_FOTA_PULL_METHOD_SUPPORTED)

#    if !defined(ANJ_FOTA_PUSH_METHOD_SUPPORTED) \
            && defined(ANJ_FOTA_PULL_METHOD_SUPPORTED)
static sdm_fw_update_handlers_t handlers_simple_pull = {
    .uri_write_handler = &user_uri_write_handler,
    .update_start_handler = &user_update_start_handler,
    .reset_handler = &user_reset_handler
};

AVS_UNIT_TEST(sdm_fw_update, simple_handlers_pull_only) {
    INIT_ENV_SDM(handlers_simple_pull);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_STRING,
        .value.bytes_or_string.data = EXAMPLE_URI,
        .value.bytes_or_string.chunk_length = strlen(EXAMPLE_URI),
        .value.bytes_or_string.full_length_hint = strlen(EXAMPLE_URI),
        .path = FLUF_MAKE_RESOURCE_PATH(5, 0, 1)
    };
    strcpy(expected_uri, EXAMPLE_URI);
    result_to_return = SDM_FW_UPDATE_RESULT_SUCCESS;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 1)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    // check if state == downloading
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_DOWNLOADING);

    // check if uri applied to the resource
    PERFORM_RESOURCE_READ(1, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, EXAMPLE_URI);

    PERFORM_RESOURCE_READ(9, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, 0);
    AVS_UNIT_ASSERT_EQUAL(out_type, FLUF_DATA_TYPE_INT);
    END_READ;

    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "56");
}
#    endif // !defined(ANJ_FOTA_PUSH_METHOD_SUPPORTED) &&
           // defined(ANJ_FOTA_PULL_METHOD_SUPPORTED)

AVS_UNIT_TEST(sdm_fw_update, write_uri) {
    INIT_ENV_SDM(handlers);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_STRING,
        .value.bytes_or_string.data = EXAMPLE_URI,
        .value.bytes_or_string.chunk_length = strlen(EXAMPLE_URI),
        .value.bytes_or_string.full_length_hint = strlen(EXAMPLE_URI),
        .path = FLUF_MAKE_RESOURCE_PATH(5, 0, 1)
    };
    strcpy(expected_uri, EXAMPLE_URI);
    result_to_return = SDM_FW_UPDATE_RESULT_SUCCESS;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 1)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    BEGIN_READ;
    // check if state == downloading
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_DOWNLOADING);
    // check if uri applied to the resource
    PERFORM_RESOURCE_READ(1, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, EXAMPLE_URI);
    END_READ;

    // cancel by empty write
    record.value.bytes_or_string.chunk_length = 0;
    record.value.bytes_or_string.full_length_hint = 0;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 1)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    // check if state == idle
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    // check if uri applied to the resource
    PERFORM_RESOURCE_READ(1, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, "");
    END_READ;

    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "37");

    // check if a wrong URI works properly
    record.value.bytes_or_string.data = "wrong::uri";
    record.value.bytes_or_string.chunk_length = strlen("wrong::uri") + 1;
    record.value.bytes_or_string.full_length_hint = strlen("wrong::uri") + 1;
    strcpy(expected_uri, "wrong::uri");
    result_to_return = SDM_FW_UPDATE_RESULT_INVALID_URI;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 1)));
    AVS_UNIT_ASSERT_FAILED(sdm_write_entry(&sdm, &record));
    int ret = sdm_operation_end(&sdm);
    AVS_UNIT_ASSERT_EQUAL(ret, SDM_ERR_BAD_REQUEST);

    // check if state == idle
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    // check if uri applied to the resource
    PERFORM_RESOURCE_READ(1, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL_STRING(val.bytes_or_string.data, "wrong::uri");
    // check result applied
    PERFORM_RESOURCE_READ(5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_RESULT_INVALID_URI);
    END_READ;
}

AVS_UNIT_TEST(sdm_fw_update, write_package_success) {
    INIT_ENV_SDM(handlers);

    // reset the testing buffer
    memset(package_buffer, '\0', sizeof(package_buffer));
    package_buffer_offset = 0;

    // write partial data
    uint8_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = 1;
    }
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_BYTES,
        .value.bytes_or_string.data = data,
        .value.bytes_or_string.chunk_length = 250,
        .value.bytes_or_string.full_length_hint = 256,
        .path = FLUF_MAKE_RESOURCE_PATH(5, 0, 0)
    };
    result_to_return = SDM_FW_UPDATE_RESULT_SUCCESS;

    // can't set the result in idle state
    AVS_UNIT_ASSERT_FAILED(
            sdm_fw_update_object_set_download_result(&entity_ctx, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    // write start and write
    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "01");

    // check if state == IDLE
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    END_READ;

    // write the rest
    record.value.bytes_or_string.chunk_length = 6;
    record.value.bytes_or_string.offset = 250;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    // write start, 2 writes and write finish
    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "0112");

    // check if state == DOWNLOADED and result applied
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_DOWNLOADED);
    PERFORM_RESOURCE_READ(5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_RESULT_INITIAL);
    END_READ;

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(package_buffer, data, 256);
}

AVS_UNIT_TEST(sdm_fw_update, write_package_failed) {
    INIT_ENV_SDM(handlers);

    // reset the testing buffer
    memset(package_buffer, '\0', sizeof(package_buffer));
    package_buffer_offset = 0;

    // write partial data
    uint8_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = 1;
    }
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_BYTES,
        .value.bytes_or_string.data = data,
        .value.bytes_or_string.chunk_length = 250,
        .value.bytes_or_string.full_length_hint = 256,
        .path = FLUF_MAKE_RESOURCE_PATH(5, 0, 0)
    };
    result_to_return = SDM_FW_UPDATE_RESULT_SUCCESS;

    // can't set the result in idle state
    AVS_UNIT_ASSERT_FAILED(
            sdm_fw_update_object_set_download_result(&entity_ctx, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    // write start and write
    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "01");

    // check if state == idle
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    END_READ;

    // write the rest
    record.value.bytes_or_string.chunk_length = 6;
    record.value.bytes_or_string.offset = 250;
    result_to_return = SDM_FW_UPDATE_RESULT_FAILED;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 0)));
    AVS_UNIT_ASSERT_FAILED(sdm_write_entry(&sdm, &record));
    int ret = sdm_operation_end(&sdm);
    AVS_UNIT_ASSERT_EQUAL(ret, SDM_ERR_INTERNAL);
    // write start, write, write finish
    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "0117");

    // check if state == IDLE and result applied
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    PERFORM_RESOURCE_READ(5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_RESULT_FAILED);
    END_READ;

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(package_buffer, data, 256);
}

AVS_UNIT_TEST(sdm_fw_update, write_package_failed_integrity) {
    INIT_ENV_SDM(handlers);

    // reset the testing buffer
    memset(package_buffer, '\0', sizeof(package_buffer));
    package_buffer_offset = 0;

    // write partial data
    uint8_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = 1;
    }
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_BYTES,
        .value.bytes_or_string.data = data,
        .value.bytes_or_string.chunk_length = 250,
        .value.bytes_or_string.full_length_hint = 256,
        .path = FLUF_MAKE_RESOURCE_PATH(5, 0, 0)
    };
    result_to_return = SDM_FW_UPDATE_RESULT_SUCCESS;

    // can't set the result in idle state
    AVS_UNIT_ASSERT_FAILED(
            sdm_fw_update_object_set_download_result(&entity_ctx, 0));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));
    // write start and write
    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "01");

    // check if state == idle
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    END_READ;

    // write the rest
    record.value.bytes_or_string.chunk_length = 6;
    record.value.bytes_or_string.offset = 250;
    result_to_return = SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 0)));
    AVS_UNIT_ASSERT_FAILED(sdm_write_entry(&sdm, &record));
    int ret = sdm_operation_end(&sdm);
    AVS_UNIT_ASSERT_EQUAL(ret, SDM_ERR_INTERNAL);
    // write start, write, write finish and reset
    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "0117");

    // check if state == IDLE and result applied
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    PERFORM_RESOURCE_READ(5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value,
                          SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE);
    END_READ;

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(package_buffer, data, 256);
}

AVS_UNIT_TEST(sdm_fw_update, execute) {
    INIT_ENV_SDM(handlers);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_STRING,
        .value.bytes_or_string.data = EXAMPLE_URI,
        .value.bytes_or_string.chunk_length = strlen(EXAMPLE_URI),
        .value.bytes_or_string.full_length_hint = strlen(EXAMPLE_URI),
        .path = FLUF_MAKE_RESOURCE_PATH(5, 0, 1)
    };
    strcpy(expected_uri, EXAMPLE_URI);
    result_to_return = SDM_FW_UPDATE_RESULT_SUCCESS;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 1)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&sdm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    AVS_UNIT_ASSERT_SUCCESS(sdm_fw_update_object_set_download_result(
            &entity_ctx, SDM_FW_UPDATE_RESULT_SUCCESS));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&sdm, FLUF_OP_DM_EXECUTE, false,
                                &FLUF_MAKE_RESOURCE_PATH(5, 0, 2)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_execute(&sdm, NULL, 0));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&sdm));

    // check if state == UPDATING
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_UPDATING);
    PERFORM_RESOURCE_READ(5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_RESULT_INITIAL);
    END_READ;

    sdm_fw_update_object_set_update_result(&entity_ctx,
                                           SDM_FW_UPDATE_RESULT_SUCCESS);

    // check if state == IDLE and result did not apply
    BEGIN_READ;
    PERFORM_RESOURCE_READ(3, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_STATE_IDLE);
    PERFORM_RESOURCE_READ(5, SUCCESS);
    AVS_UNIT_ASSERT_EQUAL(val.int_value, SDM_FW_UPDATE_RESULT_SUCCESS);
    END_READ;

    AVS_UNIT_ASSERT_EQUAL_STRING(user_arg.order, "34");
}

#endif // ANJ_WITH_FOTA_OBJECT
