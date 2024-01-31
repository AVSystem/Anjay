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
static bool inst_create_return_eror;
static int call_counter_create;
static fluf_iid_t call_iid;
static sdm_op_result_t call_result;

static int operation_begin(sdm_obj_t *obj, fluf_op_t operation) {
    (void) obj;
    (void) operation;
    call_counter_begin++;
    return 0;
}

static int operation_end(sdm_obj_t *obj, sdm_op_result_t result) {
    (void) obj;
    call_result = result;
    call_counter_end++;
    return 0;
}

sdm_obj_inst_t new_0;
sdm_res_spec_t res_spec_new = {
    .rid = 7,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_DOUBLE
};
sdm_res_t res_new[] = {
    {
        .res_spec = &res_spec_new,
    }
};
sdm_obj_inst_t new_1 = {
    .res_count = 1,
    .resources = res_new
};
sdm_obj_inst_t new_2;
static int
inst_create(sdm_obj_t *obj, sdm_obj_inst_t **out_obj_inst, fluf_iid_t iid) {
    (void) obj;
    (void) obj;
    call_iid = iid;
    if (!call_counter_create) {
        *out_obj_inst = &new_0;
    } else if (call_counter_create == 1) {
        *out_obj_inst = &new_1;
    } else {
        *out_obj_inst = &new_2;
    }
    call_counter_create++;
    if (inst_create_return_eror) {
        return -1;
    }
    return 0;
}

static int operation_validate(sdm_obj_t *obj) {
    (void) obj;
    call_counter_validate++;
    return 0;
}

#define TEST_INIT(Dm, Obj)                                       \
    sdm_res_spec_t res_spec_0 = {                                \
        .rid = 0,                                                \
        .operation = SDM_RES_RW,                                 \
        .type = FLUF_DATA_TYPE_INT                               \
    };                                                           \
    sdm_res_t res_1[] = {                                        \
        {                                                        \
            .res_spec = &res_spec_0,                             \
        }                                                        \
    };                                                           \
    sdm_obj_inst_t obj_inst_1 = {                                \
        .iid = 1,                                                \
        .res_count = 1,                                          \
        .resources = res_1                                       \
    };                                                           \
    sdm_obj_inst_t obj_inst_3 = {                                \
        .iid = 3,                                                \
    };                                                           \
    sdm_obj_inst_t *obj_insts[5] = { &obj_inst_1, &obj_inst_3 }; \
    sdm_obj_handlers_t handlers = {                              \
        .operation_begin = operation_begin,                      \
        .operation_end = operation_end,                          \
        .operation_validate = operation_validate,                \
        .inst_create = inst_create                               \
    };                                                           \
    sdm_obj_t Obj = {                                            \
        .oid = 1,                                                \
        .insts = obj_insts,                                      \
        .inst_count = 2,                                         \
        .obj_handlers = &handlers,                               \
        .max_inst_count = 5                                      \
    };                                                           \
    sdm_data_model_t Dm;                                         \
    sdm_obj_t *Objs[1];                                          \
    sdm_initialize(&Dm, Objs, 1);                                \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&Dm, &Obj));             \
    call_counter_begin = 0;                                      \
    call_counter_end = 0;                                        \
    call_counter_validate = 0;                                   \
    call_counter_create = 0;                                     \
    call_iid = 777;                                              \
    call_result = 4;

AVS_UNIT_TEST(sdm_create, create) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_iid, 0);

    path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_iid, 2);

    path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_iid, 4);

    AVS_UNIT_ASSERT_TRUE(obj_insts[0] == &new_0);
    AVS_UNIT_ASSERT_TRUE(obj_insts[1] == &obj_inst_1);
    AVS_UNIT_ASSERT_TRUE(obj_insts[2] == &new_1);
    AVS_UNIT_ASSERT_TRUE(obj_insts[3] == &obj_inst_3);
    AVS_UNIT_ASSERT_TRUE(obj_insts[4] == &new_2);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[0]->iid, new_0.iid);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[1]->iid, obj_inst_1.iid);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[2]->iid, new_1.iid);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[3]->iid, obj_inst_3.iid);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[4]->iid, new_2.iid);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_create, 3);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_create, create_with_write) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_iid, 0);

    path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_DOUBLE,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 2, 7),
        .value.double_value = 17.25
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_write_entry(&dm, &record));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_iid, 2);

    path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_iid, 4);

    AVS_UNIT_ASSERT_TRUE(obj_insts[0] == &new_0);
    AVS_UNIT_ASSERT_TRUE(obj_insts[1] == &obj_inst_1);
    AVS_UNIT_ASSERT_TRUE(obj_insts[2] == &new_1);
    AVS_UNIT_ASSERT_TRUE(obj_insts[3] == &obj_inst_3);
    AVS_UNIT_ASSERT_TRUE(obj_insts[4] == &new_2);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[0]->iid, new_0.iid);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[1]->iid, obj_inst_1.iid);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[2]->iid, new_1.iid);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[3]->iid, obj_inst_3.iid);
    AVS_UNIT_ASSERT_EQUAL(obj_insts[4]->iid, new_2.iid);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_create, 3);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    AVS_UNIT_ASSERT_EQUAL(res_new[0].value.res_value.value.double_value, 17.25);
}

AVS_UNIT_TEST(sdm_create, create_error_write_path) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_iid, 0);

    path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    fluf_io_out_entry_t record = {
        .type = FLUF_DATA_TYPE_DOUBLE,
        .path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0),
        .value.int_value = 1
    };
    AVS_UNIT_ASSERT_EQUAL(sdm_write_entry(&dm, &record),
                          SDM_ERR_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_METHOD_NOT_ALLOWED);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_create, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
}

AVS_UNIT_TEST(sdm_create, callback_error) {
    TEST_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    inst_create_return_eror = true;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), -1);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_create, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
    inst_create_return_eror = false;
}

AVS_UNIT_TEST(sdm_create, error_no_space) {
    TEST_INIT(dm, obj);

    obj.max_inst_count = 3;
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_iid, 0);

    path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_CREATE, false, &path),
            SDM_ERR_MEMORY);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_create, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}
