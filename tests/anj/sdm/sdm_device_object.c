/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm.h>
#include <anj/sdm_device_object.h>

#define SDM_INITIALIZE_BASIC(DmObj)                            \
    sdm_data_model_t DmObj;                                    \
    sdm_obj_t *objs_array[3];                                  \
    uint16_t objs_array_size = 3;                              \
    sdm_initialize(&(DmObj), objs_array, objs_array_size);     \
    AVS_UNIT_ASSERT_EQUAL((DmObj).max_allowed_objs_number, 3); \
    sdm_obj_t obj_1 = {                                        \
        .oid = 1                                               \
    };                                                         \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&(DmObj), &obj_1));    \
    sdm_obj_t obj_2 = {                                        \
        .oid = 2,                                              \
        .version = "2.2"                                       \
    };                                                         \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&(DmObj), &obj_2));

#define VERIFY_INT_ENTRY(Out, Path, Value)                          \
    AVS_UNIT_ASSERT_TRUE(fluf_uri_path_equal(&(Out).path, (Path))); \
    AVS_UNIT_ASSERT_EQUAL((Out).value.int_value, (Value));          \
    AVS_UNIT_ASSERT_EQUAL((Out).type, FLUF_DATA_TYPE_INT);

#define VERIFY_STR_ENTRY(Out, Path, Value)                                   \
    AVS_UNIT_ASSERT_TRUE(fluf_uri_path_equal(&(Out).path, (Path)));          \
    AVS_UNIT_ASSERT_EQUAL_STRING((Out).value.bytes_or_string.data, (Value)); \
    AVS_UNIT_ASSERT_EQUAL((Out).type, FLUF_DATA_TYPE_STRING);

#define CHECK_AND_VERIFY_STRING_RESOURCE(DmObj, Path, Value, Record, Count)  \
    AVS_UNIT_ASSERT_SUCCESS(                                                 \
            sdm_operation_begin(&(DmObj), FLUF_OP_DM_READ, false, &(Path))); \
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&(DmObj), &(Count))); \
    AVS_UNIT_ASSERT_EQUAL(1, (Count));                                       \
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&(DmObj), &(Record)),           \
                          SDM_LAST_RECORD);                                  \
    VERIFY_STR_ENTRY((Record), &(Path), (Value));                            \
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&(DmObj)));

#define MANUFACTURER_STR "manufacturer"
#define MODEL_NUMBER_STR "model_number"
#define SERIAL_NUMBER_STR "serial_number"
#define FIRMWARE_VERSION_STR "firmware_version"
#define SUPPORTED_BINDING_MODES_STR "UMT"

static int g_reboot_execute_counter;

static int reboot_cb(sdm_obj_t *obj,
                     sdm_obj_inst_t *obj_inst,
                     sdm_res_t *res,
                     const char *execute_arg,
                     size_t execute_arg_len) {
    (void) obj;
    (void) obj_inst;
    (void) res;
    (void) execute_arg;
    (void) execute_arg_len;

    g_reboot_execute_counter++;

    return 0;
}

AVS_UNIT_TEST(sdm_device_object, add_remove_objects) {
    SDM_INITIALIZE_BASIC(dm_test);
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 2);

    sdm_device_object_init_t dev_obj_init = {
        .manufacturer = MANUFACTURER_STR,
        .model_number = MODEL_NUMBER_STR,
        .serial_number = SERIAL_NUMBER_STR,
        .firmware_version = FIRMWARE_VERSION_STR,
        .reboot_handler = reboot_cb,
        .supported_binding_modes = SUPPORTED_BINDING_MODES_STR
    };

    AVS_UNIT_ASSERT_SUCCESS(sdm_device_object_install(&dm_test, &dev_obj_init));
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 3);

    AVS_UNIT_ASSERT_FAILED(sdm_device_object_install(&dm_test, &dev_obj_init));
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 3);

    AVS_UNIT_ASSERT_SUCCESS(sdm_remove_obj(&dm_test, 3));
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 2);
}

AVS_UNIT_TEST(sdm_device_object, resources_execute) {
    SDM_INITIALIZE_BASIC(dm_test);
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 2);

    sdm_device_object_init_t dev_obj_init = {
        .manufacturer = MANUFACTURER_STR,
        .model_number = MODEL_NUMBER_STR,
        .serial_number = SERIAL_NUMBER_STR,
        .firmware_version = FIRMWARE_VERSION_STR,
        .reboot_handler = reboot_cb,
        .supported_binding_modes = SUPPORTED_BINDING_MODES_STR
    };

    AVS_UNIT_ASSERT_SUCCESS(sdm_device_object_install(&dm_test, &dev_obj_init));
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 3);

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm_test,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(3, 0, 4)));
    AVS_UNIT_ASSERT_EQUAL(g_reboot_execute_counter, 0);
    AVS_UNIT_ASSERT_SUCCESS(sdm_execute(&dm_test, NULL, 0));
    AVS_UNIT_ASSERT_EQUAL(g_reboot_execute_counter, 1);
    AVS_UNIT_ASSERT_SUCCESS(sdm_execute(&dm_test, NULL, 0));
    AVS_UNIT_ASSERT_EQUAL(g_reboot_execute_counter, 2);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm_test));

    AVS_UNIT_ASSERT_FAILED(
            sdm_operation_begin(&dm_test,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(3, 0, 0)));
    AVS_UNIT_ASSERT_FAILED(sdm_operation_end(&dm_test));
    AVS_UNIT_ASSERT_FAILED(
            sdm_operation_begin(&dm_test,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(3, 0, 1)));
    AVS_UNIT_ASSERT_FAILED(sdm_operation_end(&dm_test));
    AVS_UNIT_ASSERT_FAILED(
            sdm_operation_begin(&dm_test,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(3, 0, 2)));
    AVS_UNIT_ASSERT_FAILED(sdm_operation_end(&dm_test));
    AVS_UNIT_ASSERT_FAILED(
            sdm_operation_begin(&dm_test,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(3, 0, 3)));
    AVS_UNIT_ASSERT_FAILED(sdm_operation_end(&dm_test));
    AVS_UNIT_ASSERT_FAILED(
            sdm_operation_begin(&dm_test,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(3, 0, 11)));
    AVS_UNIT_ASSERT_FAILED(sdm_operation_end(&dm_test));
    AVS_UNIT_ASSERT_FAILED(
            sdm_operation_begin(&dm_test,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(3, 0, 16)));
}

AVS_UNIT_TEST(sdm_device_object, execute_on_missing_resource) {
    SDM_INITIALIZE_BASIC(dm_test);
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 2);

    sdm_device_object_init_t dev_obj_init = {
        .manufacturer = MANUFACTURER_STR,
        .model_number = MODEL_NUMBER_STR,
        .serial_number = SERIAL_NUMBER_STR,
        .firmware_version = FIRMWARE_VERSION_STR,
        .reboot_handler = NULL,
        .supported_binding_modes = SUPPORTED_BINDING_MODES_STR
    };

    AVS_UNIT_ASSERT_SUCCESS(sdm_device_object_install(&dm_test, &dev_obj_init));
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 3);

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm_test,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(3, 0, 4)));
    AVS_UNIT_ASSERT_FAILED(sdm_execute(&dm_test, NULL, 0));
    AVS_UNIT_ASSERT_FAILED(sdm_operation_end(&dm_test));
}

AVS_UNIT_TEST(sdm_device_object, resources_read) {
    SDM_INITIALIZE_BASIC(dm_test);
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 2);

    sdm_device_object_init_t dev_obj_init = {
        .manufacturer = MANUFACTURER_STR,
        .model_number = MODEL_NUMBER_STR,
        .serial_number = SERIAL_NUMBER_STR,
        .firmware_version = FIRMWARE_VERSION_STR,
        .reboot_handler = NULL,
        .supported_binding_modes = SUPPORTED_BINDING_MODES_STR
    };

    AVS_UNIT_ASSERT_SUCCESS(sdm_device_object_install(&dm_test, &dev_obj_init));
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 3);

    size_t out_res_count = 0;
    fluf_io_out_entry_t out_record = { 0 };

    CHECK_AND_VERIFY_STRING_RESOURCE(dm_test,
                                     FLUF_MAKE_RESOURCE_PATH(3, 0, 0),
                                     MANUFACTURER_STR,
                                     out_record,
                                     out_res_count);
    CHECK_AND_VERIFY_STRING_RESOURCE(dm_test,
                                     FLUF_MAKE_RESOURCE_PATH(3, 0, 1),
                                     MODEL_NUMBER_STR,
                                     out_record,
                                     out_res_count);
    CHECK_AND_VERIFY_STRING_RESOURCE(dm_test,
                                     FLUF_MAKE_RESOURCE_PATH(3, 0, 2),
                                     SERIAL_NUMBER_STR,
                                     out_record,
                                     out_res_count);
    CHECK_AND_VERIFY_STRING_RESOURCE(dm_test,
                                     FLUF_MAKE_RESOURCE_PATH(3, 0, 3),
                                     FIRMWARE_VERSION_STR,
                                     out_record,
                                     out_res_count);
    CHECK_AND_VERIFY_STRING_RESOURCE(dm_test,
                                     FLUF_MAKE_RESOURCE_PATH(3, 0, 16),
                                     SUPPORTED_BINDING_MODES_STR,
                                     out_record,
                                     out_res_count);
}

AVS_UNIT_TEST(sdm_device_object, err_codes) {
    SDM_INITIALIZE_BASIC(dm_test);
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 2);

    sdm_device_object_init_t dev_obj_init = {
        .manufacturer = MANUFACTURER_STR,
        .model_number = MODEL_NUMBER_STR,
        .serial_number = SERIAL_NUMBER_STR,
        .firmware_version = FIRMWARE_VERSION_STR,
        .reboot_handler = NULL,
        .supported_binding_modes = SUPPORTED_BINDING_MODES_STR
    };

    AVS_UNIT_ASSERT_SUCCESS(sdm_device_object_install(&dm_test, &dev_obj_init));
    AVS_UNIT_ASSERT_EQUAL(dm_test.objs_count, 3);

    size_t out_res_count = 0;
    fluf_io_out_entry_t out_record = { 0 };
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(3, 0, 11);

    // object initialized - no error code
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm_test, FLUF_OP_DM_READ, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_get_readable_res_count(&dm_test, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(out_res_count, 1);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm_test, &out_record),
                          SDM_LAST_RECORD);
    VERIFY_INT_ENTRY(
            out_record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 11, 0), 0);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm_test));
}
