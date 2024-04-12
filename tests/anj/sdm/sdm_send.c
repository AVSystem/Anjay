/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_time.h>
#include <avsystem/commons/avs_unit_test.h>

#include <anj/sdm.h>
#include <anj/sdm_io.h>
#include <anj/sdm_send.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

static int res_execute(sdm_obj_t *obj,
                       sdm_obj_inst_t *obj_inst,
                       sdm_res_t *res,
                       const char *execute_arg,
                       size_t execute_arg_len) {
    (void) obj;
    (void) obj_inst;
    (void) res;
    (void) execute_arg;
    (void) execute_arg_len;
    return 0;
}

static sdm_res_handlers_t res_handlers = {
    .res_execute = res_execute
};

// serial number
static sdm_res_spec_t res_spec_2 = {
    .rid = 2,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_STRING
};

// firmware version
static sdm_res_spec_t res_spec_3 = {
    .rid = 3,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_STRING
};

// reboot
static sdm_res_spec_t res_spec_4 = {
    .rid = 4,
    .operation = SDM_RES_E
};

// battery level
static sdm_res_spec_t res_spec_9 = {
    .rid = 9,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};

// error code
static sdm_res_spec_t res_spec_11 = {
    .rid = 11,
    .operation = SDM_RES_RM,
    .type = FLUF_DATA_TYPE_INT
};

// error code, instance 0
static sdm_res_inst_t res_inst_0 = {
    .riid = 0,
    .res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(1))
};

// error code, instance 1
static sdm_res_inst_t res_inst_1 = {
    .riid = 1,
    .res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(6))
};

static sdm_res_inst_t *res_insts[2] = { &res_inst_0, &res_inst_1 };
static sdm_res_t res_object_3[] = {
    {
        .res_spec = &res_spec_2,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_STRING("SN:1234567890"))
    },
    {
        .res_spec = &res_spec_3,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_STRING("dummy_firmware"))
    },
    {
        .res_spec = &res_spec_4,
        .res_handlers = &res_handlers
    },
    {
        .res_spec = &res_spec_9,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(87))
    },
    {
        .res_spec = &res_spec_11,
        .value.res_inst.max_inst_count = 10,
        .value.res_inst.inst_count = 2,
        .value.res_inst.insts = res_insts
    }
};

static sdm_obj_inst_t obj_3_inst_1 = {
    .iid = 0,
    .res_count = 5,
    .resources = res_object_3
};
static sdm_obj_inst_t *obj_3_insts[1] = { &obj_3_inst_1 };
static sdm_obj_t obj_3 = {
    .oid = 3,
    .insts = obj_3_insts,
    .inst_count = 1,
    .max_inst_count = 1
};

#define MOCK_DATA_MODEL(dm)          \
    sdm_data_model_t dm;             \
    sdm_obj_t *objects[1];           \
    sdm_initialize(&dm, objects, 1); \
    sdm_add_obj(&sdm, &obj_3);

AVS_UNIT_TEST(sdm_send, create_msg_from_dm_single_path) {
    MOCK_DATA_MODEL(sdm);

    fluf_uri_path_t path[] = { FLUF_MAKE_RESOURCE_PATH(3, 0, 3) };

    uint8_t out_buff[50];
    size_t inout_size = sizeof(out_buff);

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_send_create_msg_from_dm(&sdm,
                                        FLUF_COAP_FORMAT_SENML_CBOR,
                                        1705597224.0,
                                        out_buff,
                                        &inout_size,
                                        path,
                                        AVS_ARRAY_SIZE(path)));
    AVS_UNIT_ASSERT_EQUAL(inout_size, 36);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            out_buff,
            "\x81\xa3"
            "\x00\x66/3/0/3"                           // path
            "\x22\xfb\x41\xd9\x6a\x56\x4a\x00\x00\x00" // base time
            "\x03\x6e"
            "dummy_firmware", // string value
            36);
}

AVS_UNIT_TEST(sdm_send, create_msg_from_dm_multi_path) {
    MOCK_DATA_MODEL(sdm);

    fluf_uri_path_t path[] = { FLUF_MAKE_RESOURCE_PATH(3, 0, 9),
                               FLUF_MAKE_RESOURCE_PATH(3, 0, 3) };

    uint8_t out_buff[50];
    size_t inout_size = sizeof(out_buff);

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_send_create_msg_from_dm(&sdm,
                                        FLUF_COAP_FORMAT_SENML_CBOR,
                                        1705597224.0,
                                        out_buff,
                                        &inout_size,
                                        path,
                                        AVS_ARRAY_SIZE(path)));
    AVS_UNIT_ASSERT_EQUAL(inout_size, 48);

    // TODO:MZ try mocking the clock value so the timestamp will always be the
    // same skip checking timestamp in the out_buff
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            out_buff,
            "\x82\xa3"
            "\x00\x66/3/0/9"                           // path
            "\x22\xfb\x41\xd9\x6a\x56\x4a\x00\x00\x00" // base time
            "\x02\x18\x57"                             // value 87
            "\xa2"                                     // map(2)
            "\x00\x66/3/0/3"                           // path
            "\x03\x6e"
            "dummy_firmware", // string value
            48);
}

AVS_UNIT_TEST(sdm_send, create_msg_buffer_to_small) {
    MOCK_DATA_MODEL(sdm);

    fluf_uri_path_t path[] = { FLUF_MAKE_RESOURCE_PATH(3, 0, 2),
                               FLUF_MAKE_RESOURCE_PATH(3, 0, 3),
                               FLUF_MAKE_RESOURCE_PATH(3, 0, 9) };

    uint8_t small_buff[50];
    uint8_t big_buff[100];
    size_t inout_size_small = sizeof(small_buff);
    size_t inout_size_big = sizeof(big_buff);

    AVS_UNIT_ASSERT_EQUAL(
            sdm_send_create_msg_from_dm(&sdm,
                                        FLUF_COAP_FORMAT_SENML_CBOR,
                                        1705597224.0,
                                        small_buff,
                                        &inout_size_small,
                                        path,
                                        AVS_ARRAY_SIZE(path)),
            SDM_ERR_MEMORY);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_send_create_msg_from_dm(&sdm,
                                        FLUF_COAP_FORMAT_SENML_CBOR,
                                        1705597224.0,
                                        big_buff,
                                        &inout_size_big,
                                        path,
                                        AVS_ARRAY_SIZE(path)));
}

AVS_UNIT_TEST(sdm_send, non_readable_resource) {
    MOCK_DATA_MODEL(sdm);

    fluf_uri_path_t path[] = { FLUF_MAKE_RESOURCE_PATH(3, 0, 4) };

    uint8_t out_buff[50];
    size_t inout_size = sizeof(out_buff);

    AVS_UNIT_ASSERT_EQUAL(
            sdm_send_create_msg_from_dm(&sdm,
                                        FLUF_COAP_FORMAT_SENML_CBOR,
                                        1705597224.0,
                                        out_buff,
                                        &inout_size,
                                        path,
                                        AVS_ARRAY_SIZE(path)),
            SDM_ERR_INPUT_ARG);
}

AVS_UNIT_TEST(sdm_send, create_msg_from_dm_multiinstance_resource) {
    MOCK_DATA_MODEL(sdm);

    fluf_uri_path_t path[] = { FLUF_MAKE_RESOURCE_PATH(3, 0, 11) };

    uint8_t out_buff[50];
    size_t inout_size = sizeof(out_buff);

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_send_create_msg_from_dm(&sdm,
                                        FLUF_COAP_FORMAT_SENML_CBOR,
                                        1705597224.0,
                                        out_buff,
                                        &inout_size,
                                        path,
                                        AVS_ARRAY_SIZE(path)));
    AVS_UNIT_ASSERT_EQUAL(inout_size, 39);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            out_buff,
            "\x82\xa3"
            "\x00\x69/3/0/11/0"                        // path
            "\x22\xfb\x41\xd9\x6a\x56\x4a\x00\x00\x00" // base time
            "\x02\x01"                                 // value 1
            "\xa2"                                     // map(2)
            "\x00\x69/3/0/11/1"                        // path
            "\x02\x06",                                // value 6
            39);
}

AVS_UNIT_TEST(sdm_send, create_msg_from_records) {

    double timestamp = 1705597224.0;

    fluf_io_out_entry_t records[2];

    records[0] = (fluf_io_out_entry_t) {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 0, 9),
        .type = FLUF_DATA_TYPE_INT,
        .value.int_value = 42,
        .timestamp = timestamp
    };
    records[1] = (fluf_io_out_entry_t) {
        .path = FLUF_MAKE_RESOURCE_PATH(3, 0, 17),
        .type = FLUF_DATA_TYPE_STRING,
        .value.bytes_or_string.data = "demo_device",
        .timestamp = timestamp
    };

    uint8_t out_buff[50];
    size_t inout_size = sizeof(out_buff);

    AVS_UNIT_ASSERT_SUCCESS(sdm_send_create_msg_from_list_of_records(
            FLUF_COAP_FORMAT_SENML_CBOR,
            out_buff,
            &inout_size,
            records,
            AVS_ARRAY_SIZE(records)));
    AVS_UNIT_ASSERT_EQUAL(inout_size, 46);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            out_buff,
            "\x82\xa3"
            "\x00\x66/3/0/9"                           // path
            "\x22\xfb\x41\xd9\x6a\x56\x4a\x00\x00\x00" // base time
            "\x02\x18\x2a"                             // value 42
            "\xa2"                                     // map(2)
            "\x00\x67/3/0/17"                          // path
            "\x03\x6b"
            "demo_device", // string value
            46);
}

AVS_UNIT_TEST(sdm_send, create_msg_from_records_multiinstance_resource) {

    double timestamp = 1705597224.0;

    fluf_io_out_entry_t records[2];

    records[0] = (fluf_io_out_entry_t) {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 11, 0),
        .type = FLUF_DATA_TYPE_INT,
        .value.int_value = 1,
        .timestamp = timestamp
    };
    records[1] = (fluf_io_out_entry_t) {
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 11, 1),
        .type = FLUF_DATA_TYPE_INT,
        .value.int_value = 6,
        .timestamp = timestamp
    };

    uint8_t out_buff[50];
    size_t inout_size = sizeof(out_buff);

    AVS_UNIT_ASSERT_SUCCESS(sdm_send_create_msg_from_list_of_records(
            FLUF_COAP_FORMAT_SENML_CBOR,
            out_buff,
            &inout_size,
            records,
            AVS_ARRAY_SIZE(records)));
    AVS_UNIT_ASSERT_EQUAL(inout_size, 39);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            out_buff,
            "\x82\xa3"
            "\x00\x69/3/0/11/0"                        // path
            "\x22\xfb\x41\xd9\x6a\x56\x4a\x00\x00\x00" // base time
            "\x02\x01"                                 // value 1
            "\xa2"                                     // map(2)
            "\x00\x69/3/0/11/1"                        // path
            "\x02\x06",                                // value 6
            36);
}
