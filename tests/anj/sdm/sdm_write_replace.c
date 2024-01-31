/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm.h>
#include <anj/sdm_io.h>

static int call_counter_begin;
static int call_counter_end;
static int call_counter_validate;
static int call_counter_res_write;
static int call_counter_res_create;
static int call_counter_res_delete;
static int call_counter_reset;
static sdm_res_inst_t *call_res_inst;
static sdm_res_inst_t *call_res_inst_delete;
static sdm_res_t *call_res;
static bool inst_operation_end_return_eror;
static bool res_write_operation_return_eror;
static bool res_create_operation_return_eror;
static bool validate_return_eror;
static const fluf_res_value_t *call_value;
static sdm_op_result_t call_result;

static int res_write(sdm_obj_t *obj,
                     sdm_obj_inst_t *obj_inst,
                     sdm_res_t *res,
                     sdm_res_inst_t *res_inst,
                     const fluf_res_value_t *value) {
    (void) obj;
    (void) obj_inst;
    call_res = res;
    call_res_inst = res_inst;
    call_value = value;
    call_counter_res_write++;
    if (res_write_operation_return_eror) {
        return -123;
    }
    return 0;
}

static sdm_res_inst_t res_inst_new_1 = {
    .riid = 0xFFFF
};
static sdm_res_inst_t res_inst_new_2 = {
    .riid = 0xFFFF
};
static sdm_res_inst_t res_inst_new_3 = {
    .riid = 0xFFFF
};
static int res_inst_create(sdm_obj_t *obj,
                           sdm_obj_inst_t *obj_inst,
                           sdm_res_t *res,
                           sdm_res_inst_t **out_res_inst,
                           fluf_riid_t riid) {
    (void) obj;
    (void) obj_inst;
    (void) res;
    (void) riid;

    if (res_inst_new_1.riid == 0xFFFF) {
        *out_res_inst = &res_inst_new_1;
    } else if (res_inst_new_2.riid == 0xFFFF) {
        *out_res_inst = &res_inst_new_2;
    } else {
        *out_res_inst = &res_inst_new_3;
    }
    call_counter_res_create++;
    if (res_create_operation_return_eror) {
        return -1;
    }
    return 0;
}
static int res_inst_delete(sdm_obj_t *obj,
                           sdm_obj_inst_t *obj_inst,
                           sdm_res_t *res,
                           sdm_res_inst_t *res_inst) {
    (void) obj;
    (void) obj_inst;
    (void) res;
    call_res_inst_delete = res_inst;
    call_counter_res_delete++;
    return 0;
}

static int operation_begin(sdm_obj_t *obj, fluf_op_t operation) {
    (void) obj;
    (void) operation;
    call_counter_begin++;
    return 0;
}

static int operation_end(sdm_obj_t *obj, sdm_op_result_t result) {
    (void) obj;
    call_counter_end++;
    call_result = result;
    if (inst_operation_end_return_eror) {
        return -1;
    }
    return 0;
}

static int operation_validate(sdm_obj_t *obj) {
    (void) obj;
    call_counter_validate++;
    if (validate_return_eror) {
        return -12;
    }
    return 0;
}

static int inst_reset(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst) {
    (void) obj;
    (void) obj_inst;
    call_counter_reset++;
    return 0;
}

#define TEST_INIT(Dm, Obj)                                                    \
    sdm_res_handlers_t res_handlers = {                                       \
        .res_write = res_write,                                               \
        .res_inst_create = res_inst_create,                                   \
        .res_inst_delete = res_inst_delete                                    \
    };                                                                        \
    sdm_res_handlers_t res_handlers_2 = {                                     \
        .res_inst_create = res_inst_create,                                   \
        .res_inst_delete = res_inst_delete                                    \
    };                                                                        \
    sdm_res_spec_t res_spec_0 = {                                             \
        .rid = 0,                                                             \
        .operation = SDM_RES_RW,                                              \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_1 = {                                             \
        .rid = 1,                                                             \
        .operation = SDM_RES_RW,                                              \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_2 = {                                             \
        .rid = 2,                                                             \
        .operation = SDM_RES_BS_RW,                                           \
        .type = FLUF_DATA_TYPE_DOUBLE                                         \
    };                                                                        \
    sdm_res_spec_t res_spec_3 = {                                             \
        .rid = 3,                                                             \
        .operation = SDM_RES_RM,                                              \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_4 = {                                             \
        .rid = 4,                                                             \
        .operation = SDM_RES_RWM,                                             \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_5 = {                                             \
        .rid = 5,                                                             \
        .operation = SDM_RES_RWM,                                             \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_6 = {                                             \
        .rid = 6,                                                             \
        .operation = SDM_RES_W,                                               \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_7 = {                                             \
        .rid = 7,                                                             \
        .operation = SDM_RES_RW,                                              \
        .type = FLUF_DATA_TYPE_STRING                                         \
    };                                                                        \
    sdm_res_t res_0[] = {                                                     \
        {                                                                     \
            .res_spec = &res_spec_0,                                          \
            .res_handlers = &res_handlers                                     \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_6,                                          \
            .res_handlers = &res_handlers                                     \
        }                                                                     \
    };                                                                        \
    sdm_res_inst_t res_inst_1 = {                                             \
        .riid = 1,                                                            \
        .res_value.value.int_value = 44                                       \
    };                                                                        \
    sdm_res_inst_t res_inst_3 = {                                             \
        .riid = 3,                                                            \
        .res_value.value.int_value = 44                                       \
    };                                                                        \
    sdm_res_inst_t *res_insts[9] = { &res_inst_1, &res_inst_3 };              \
    sdm_res_inst_t *res_insts_5[1] = { &res_inst_1 };                         \
    char res_7_buff[50] = { 0 };                                              \
    sdm_res_t res_1[] = {                                                     \
        {                                                                     \
            .res_spec = &res_spec_0,                                          \
            .res_handlers = &res_handlers                                     \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_1,                                          \
            .value.res_value.value.int_value = 17                             \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_2,                                          \
            .value.res_value.value.double_value = 18.0                        \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_3,                                          \
            .value.res_inst.max_inst_count = 9,                               \
            .value.res_inst.inst_count = 0                                    \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_4,                                          \
            .value.res_inst.max_inst_count = 9,                               \
            .value.res_inst.inst_count = 2,                                   \
            .value.res_inst.insts = res_insts,                                \
            .res_handlers = &res_handlers                                     \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_5,                                          \
            .value.res_inst.max_inst_count = 2,                               \
            .value.res_inst.insts = res_insts_5,                              \
            .value.res_inst.inst_count = 1,                                   \
            .res_handlers = &res_handlers_2                                   \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_6,                                          \
            .value.res_value.value.int_value = 17                             \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_7,                                          \
            .value.res_value.value.bytes_or_string.data = res_7_buff,         \
            .value.res_value.resource_buffer_size = 50                        \
        }                                                                     \
    };                                                                        \
    sdm_obj_inst_t obj_inst_0 = {                                             \
        .iid = 0,                                                             \
        .res_count = 2,                                                       \
        .resources = res_0                                                    \
    };                                                                        \
    sdm_obj_inst_t obj_inst_1 = {                                             \
        .iid = 1,                                                             \
        .res_count = 8,                                                       \
        .resources = res_1                                                    \
    };                                                                        \
    sdm_obj_inst_t obj_inst_2 = {                                             \
        .iid = 2,                                                             \
        .res_count = 0                                                        \
    };                                                                        \
    sdm_obj_inst_t *obj_insts[3] = { &obj_inst_0, &obj_inst_1, &obj_inst_2 }; \
    sdm_obj_handlers_t handlers = {                                           \
        .operation_begin = operation_begin,                                   \
        .operation_end = operation_end,                                       \
        .operation_validate = operation_validate,                             \
        .inst_reset = inst_reset                                              \
    };                                                                        \
    sdm_obj_t Obj = {                                                         \
        .oid = 1,                                                             \
        .insts = obj_insts,                                                   \
        .inst_count = 3,                                                      \
        .obj_handlers = &handlers,                                            \
        .max_inst_count = 3                                                   \
    };                                                                        \
    sdm_data_model_t Dm;                                                      \
    sdm_obj_t *Objs[1];                                                       \
    sdm_initialize(&Dm, Objs, 1);                                             \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&Dm, &Obj));                          \
    call_counter_begin = 0;                                                   \
    call_counter_end = 0;                                                     \
    call_counter_validate = 0;                                                \
    call_counter_res_write = 0;                                               \
    call_value = NULL;                                                        \
    call_res = NULL;                                                          \
    call_res_inst = NULL;                                                     \
    call_result = 4;                                                          \
    call_counter_reset = 0;                                                   \
    call_counter_res_delete = 0;                                              \
    call_counter_res_create = 0;                                              \
    res_inst_new_1.riid = 0xFFFF;                                             \
    res_inst_new_2.riid = 0xFFFF;                                             \
    res_inst_new_3.riid = 0xFFFF;

AVS_UNIT_TEST(sdm_write_replace, write_handler) {
    TEST_INIT(dm, obj);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0)
    };
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 1);
    AVS_UNIT_ASSERT_TRUE(call_res == &res_1[0]);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == NULL);
    AVS_UNIT_ASSERT_TRUE(call_value == &record.value);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_write_replace, write_no_handler) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);

    fluf_io_out_entry_t record_1 = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 1),
        .value.int_value = 77777
    };
    fluf_io_out_entry_t record_6 = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 6),
        .value.int_value = 88888
    };

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_6));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_reset, 1);
    AVS_UNIT_ASSERT_EQUAL(res_1[1].value.res_value.value.int_value, 77777);
    AVS_UNIT_ASSERT_EQUAL(res_1[6].value.res_value.value.int_value, 88888);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_write_replace, write_string_in_chunk) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 7);

    fluf_io_out_entry_t record_1 = {
        .type = FLUF_DATA_TYPE_STRING,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 7),
        .value.bytes_or_string.data = "123",
        .value.bytes_or_string.chunk_length = 3,
    };
    fluf_io_out_entry_t record_2 = {
        .type = FLUF_DATA_TYPE_STRING,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 7),
        .value.bytes_or_string.data = "ABC",
        .value.bytes_or_string.offset = 3,
        .value.bytes_or_string.chunk_length = 3
    };
    fluf_io_out_entry_t record_3 = {
        .type = FLUF_DATA_TYPE_STRING,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 7),
        .value.bytes_or_string.data = "DEF",
        .value.bytes_or_string.offset = 6,
        .value.bytes_or_string.chunk_length = 3
    };

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_2));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_3));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_reset, 0);
    AVS_UNIT_ASSERT_SUCCESS(strcmp(res_7_buff, "123ABCDEF"));
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_write_replace, multi_res_write) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 4);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 3)
    };
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));

    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_TRUE(call_res == &res_1[4]);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_new_1);
    AVS_UNIT_ASSERT_TRUE(call_res_inst_delete == &res_inst_1);
    AVS_UNIT_ASSERT_TRUE(call_value == &record.value);
    call_value = NULL;
    call_res = NULL;
    AVS_UNIT_ASSERT_EQUAL(res_1[4].value.res_inst.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(res_insts[0] == &res_inst_new_1);

    record.path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 2);
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_TRUE(call_res == &res_1[4]);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_new_2);
    AVS_UNIT_ASSERT_TRUE(call_value == &record.value);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_create, 2);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    AVS_UNIT_ASSERT_EQUAL(res_inst_new_1.riid, 3);
    AVS_UNIT_ASSERT_EQUAL(res_inst_new_2.riid, 2);
    AVS_UNIT_ASSERT_TRUE(res_insts[0] == &res_inst_new_2);
    AVS_UNIT_ASSERT_TRUE(res_insts[1] == &res_inst_new_1);
    AVS_UNIT_ASSERT_EQUAL(res_1[4].value.res_inst.inst_count, 2);
}

AVS_UNIT_TEST(sdm_write_replace, multi_res_write_no_handler) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 5);
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .value.int_value = 555555,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 188)
    };

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(res_inst_new_1.res_value.value.int_value, 555555);
    AVS_UNIT_ASSERT_EQUAL(res_inst_new_1.riid, 188);
    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_create, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_write_replace, bootstrap_write) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);

    fluf_io_out_entry_t record_1 = {
        .type = FLUF_DATA_TYPE_DOUBLE,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 2),
        .value.double_value = 1.25
    };

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, true, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(res_1[2].value.res_value.value.double_value, 1.25);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, true, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record_1), SDM_ERR_BAD_REQUEST);
}

AVS_UNIT_TEST(sdm_write_replace, multi_res_write_create) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 4);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0)
    };
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));

    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_TRUE(call_res == &res_1[4]);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_new_1);
    AVS_UNIT_ASSERT_TRUE(call_value == &record.value);
    AVS_UNIT_ASSERT_EQUAL(res_inst_new_1.riid, 0);
    call_value = NULL;
    call_res = NULL;

    record.path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 2);
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_TRUE(call_res == &res_1[4]);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_new_2);
    AVS_UNIT_ASSERT_TRUE(call_value == &record.value);
    AVS_UNIT_ASSERT_EQUAL(res_inst_new_2.riid, 2);
    call_value = NULL;
    call_res = NULL;

    record.path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 8);
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_TRUE(call_res == &res_1[4]);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_new_3);
    AVS_UNIT_ASSERT_TRUE(call_value == &record.value);
    AVS_UNIT_ASSERT_EQUAL(res_inst_new_3.riid, 8);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 3);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    AVS_UNIT_ASSERT_TRUE(res_insts[0] == &res_inst_new_1);
    AVS_UNIT_ASSERT_TRUE(res_insts[1] == &res_inst_new_2);
    AVS_UNIT_ASSERT_TRUE(res_insts[2] == &res_inst_new_3);
    AVS_UNIT_ASSERT_TRUE(res_insts[0]->riid == 0 && res_insts[1]->riid == 2
                         && res_insts[2]->riid == 8);
}

AVS_UNIT_TEST(sdm_write_replace, error_type) {
    TEST_INIT(dm, obj);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_BOOL,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0)
    };
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record), SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_BAD_REQUEST);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
}

AVS_UNIT_TEST(sdm_write_replace, error_no_writable) {
    TEST_INIT(dm, obj);
    res_spec_0.operation = SDM_RES_R;
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0)
    };
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record), SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_BAD_REQUEST);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
}

AVS_UNIT_TEST(sdm_write_replace, error_path) {
    TEST_INIT(dm, obj);
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 12)
    };
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record), SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
}

AVS_UNIT_TEST(sdm_write_replace, error_path_multi_instance) {
    TEST_INIT(dm, obj);
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 4)
    };
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 4);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record),
                          SDM_ERR_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_METHOD_NOT_ALLOWED);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
}

AVS_UNIT_TEST(sdm_write_replace, handler_error) {
    TEST_INIT(dm, obj);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0)
    };
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0);
    res_write_operation_return_eror = true;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record), -123);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), -123);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 1);
    AVS_UNIT_ASSERT_TRUE(call_res == &res_1[0]);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == NULL);
    AVS_UNIT_ASSERT_TRUE(call_value == &record.value);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);

    res_write_operation_return_eror = false;
}

AVS_UNIT_TEST(sdm_write_replace, handler_error_2) {
    TEST_INIT(dm, obj);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0)
    };
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 4);
    res_create_operation_return_eror = true;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record), -1);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), -1);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);

    res_create_operation_return_eror = false;
}

AVS_UNIT_TEST(sdm_write_replace, handler_error_3) {
    TEST_INIT(dm, obj);

    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_INT,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0)
    };
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 4);
    validate_return_eror = true;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), -12);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);

    validate_return_eror = false;
}

AVS_UNIT_TEST(sdm_write_replace, string_in_chunk_error) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 7);

    fluf_io_out_entry_t record_1 = {
        .type = FLUF_DATA_TYPE_STRING,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 7),
        .value.bytes_or_string.data = "123",
        .value.bytes_or_string.chunk_length = 3,
    };
    fluf_io_out_entry_t record_2 = {
        .type = FLUF_DATA_TYPE_STRING,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 7),
        .value.bytes_or_string.data = "ABC",
        .value.bytes_or_string.offset = 3,
        .value.bytes_or_string.chunk_length = 3
    };
    fluf_io_out_entry_t record_3 = {
        .type = FLUF_DATA_TYPE_STRING,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 7),
        .value.bytes_or_string.data = "DEF",
        .value.bytes_or_string.offset = 6,
        .value.bytes_or_string.chunk_length = 3
    };

    res_1[7].value.res_value.resource_buffer_size = 7;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record_2));
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record_3), SDM_ERR_MEMORY);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_MEMORY);
    res_1[7].value.res_value.resource_buffer_size = 50;

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_write, 0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
}

AVS_UNIT_TEST(sdm_write_replace, lack_of_inst_reset_error) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    handlers.inst_reset = NULL;
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_WRITE_REPLACE, false, &path),
            SDM_ERR_INTERNAL);
}
