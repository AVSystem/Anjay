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
static int call_counter_delete;
static int call_counter_res_delete;
static sdm_obj_inst_t *call_obj_inst;
static sdm_res_inst_t *call_res_inst;
static sdm_res_t *call_res;
static bool inst_delete_return_eror;
static bool inst_operation_end_return_eror;
static bool res_inst_operation_return_eror;
static sdm_op_result_t call_result;

static int res_inst_delete(sdm_obj_t *obj,
                           sdm_obj_inst_t *obj_inst,
                           sdm_res_t *res,
                           sdm_res_inst_t *res_inst) {
    (void) obj;
    (void) obj_inst;
    call_res = res;
    call_res_inst = res_inst;
    call_counter_res_delete++;
    if (res_inst_operation_return_eror) {
        return -1;
    }
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
    call_result = result;
    call_counter_end++;
    if (inst_operation_end_return_eror) {
        return -22;
    }
    return 0;
}

static int operation_validate(sdm_obj_t *obj) {
    (void) obj;
    call_counter_validate++;
    return 0;
}

static int inst_delete(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst) {
    (void) obj;
    call_counter_delete++;
    call_obj_inst = obj_inst;
    if (inst_delete_return_eror) {
        return -1;
    }
    return 0;
}

#define TEST_INIT(Dm, Obj)                                                    \
    sdm_res_handlers_t res_handlers = {                                       \
        .res_inst_delete = res_inst_delete                                    \
    };                                                                        \
    sdm_res_spec_t res_spec_0 = {                                             \
        .rid = 0,                                                             \
        .operation = SDM_RES_R,                                               \
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
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_3 = {                                             \
        .rid = 3,                                                             \
        .operation = SDM_RES_RM,                                              \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_4 = {                                             \
        .rid = 4,                                                             \
        .operation = SDM_RES_RM,                                              \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_5 = {                                             \
        .rid = 5,                                                             \
        .operation = SDM_RES_RM,                                              \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_spec_t res_spec_write = {                                         \
        .rid = 6,                                                             \
        .operation = SDM_RES_W,                                               \
        .type = FLUF_DATA_TYPE_INT                                            \
    };                                                                        \
    sdm_res_t res_0[] = {                                                     \
        {                                                                     \
            .res_spec = &res_spec_0,                                          \
            .res_handlers = &res_handlers                                     \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_write,                                      \
            .res_handlers = &res_handlers                                     \
        }                                                                     \
    };                                                                        \
    sdm_res_inst_t res_inst_0 = {                                             \
        .riid = 0,                                                            \
        .res_value.value.int_value = 33                                       \
    };                                                                        \
    sdm_res_inst_t res_inst_1 = {                                             \
        .riid = 1,                                                            \
        .res_value.value.int_value = 44                                       \
    };                                                                        \
    sdm_res_inst_t res_inst_2 = {                                             \
        .riid = 2,                                                            \
        .res_value.value.int_value = 44                                       \
    };                                                                        \
    sdm_res_inst_t *res_insts[9] = { &res_inst_0, &res_inst_1, &res_inst_2 }; \
    sdm_res_inst_t *res_insts_2[9] = { &res_inst_0 };                         \
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
            .value.res_value.value.int_value = 18                             \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_3,                                          \
            .value.res_inst.max_inst_count = 0,                               \
            .value.res_inst.inst_count = 0                                    \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_4,                                          \
            .value.res_inst.max_inst_count = 9,                               \
            .value.res_inst.inst_count = 3,                                   \
            .value.res_inst.insts = res_insts,                                \
            .res_handlers = &res_handlers                                     \
        },                                                                    \
        {                                                                     \
            .res_spec = &res_spec_5,                                          \
            .res_handlers = &res_handlers,                                    \
            .value.res_inst.max_inst_count = 9,                               \
            .value.res_inst.inst_count = 1,                                   \
            .value.res_inst.insts = res_insts_2                               \
        }                                                                     \
    };                                                                        \
    sdm_obj_inst_t obj_inst_0 = {                                             \
        .iid = 0,                                                             \
        .res_count = 2,                                                       \
        .resources = res_0                                                    \
    };                                                                        \
    sdm_obj_inst_t obj_inst_1 = {                                             \
        .iid = 1,                                                             \
        .res_count = 6,                                                       \
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
        .inst_delete = inst_delete                                            \
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
    call_counter_delete = 0;                                                  \
    call_counter_res_delete = 0;                                              \
    call_obj_inst = NULL;                                                     \
    call_res = NULL;                                                          \
    call_result = 4;                                                          \
    call_res_inst = NULL;

AVS_UNIT_TEST(sdm_delete, delete_last) {
    TEST_INIT(dm, obj);
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 1);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_2);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_delete, delete_first) {
    TEST_INIT(dm, obj);
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 1);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_delete, delete_middle) {
    TEST_INIT(dm, obj);
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_delete, delete_all) {
    TEST_INIT(dm, obj);
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    path = FLUF_MAKE_INSTANCE_PATH(1, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 2);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_2);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    path = FLUF_MAKE_INSTANCE_PATH(1, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 0);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 3);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_delete, delete_error_no_exist) {
    TEST_INIT(dm, obj);
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 4);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 3);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 1);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[2]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
}

AVS_UNIT_TEST(sdm_delete, delete_error_removed) {
    TEST_INIT(dm, obj);
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_1);
}

AVS_UNIT_TEST(sdm_delete, delete_error_no_callback) {
    TEST_INIT(dm, obj);
    obj.obj_handlers = NULL;
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 0);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path),
            SDM_ERR_INTERNAL);
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 3);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 1);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[2]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
}

AVS_UNIT_TEST(sdm_delete, delete_error_callback_error_1) {
    TEST_INIT(dm, obj);
    inst_delete_return_eror = true;
    call_result = true;

    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 0);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path), -1);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), -1);
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 3);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 1);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[2]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
    inst_delete_return_eror = false;
}

AVS_UNIT_TEST(sdm_delete, delete_error_callback_error_2) {
    TEST_INIT(dm, obj);
    inst_operation_end_return_eror = true;
    call_result = 4;

    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), -22);
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 1);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
    inst_operation_end_return_eror = false;
}

#ifdef FLUF_WITH_LWM2M12
AVS_UNIT_TEST(sdm_delete, delete_res_last) {
    TEST_INIT(dm, obj);
    sdm_res_t *res = &res_1[4];
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(obj.inst_count, 3);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[0]->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[1]->iid, 1);
    AVS_UNIT_ASSERT_EQUAL(obj.insts[2]->iid, 2);

    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[0]->riid, 0);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[1]->riid, 1);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_2);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_delete, delete_res_first) {
    TEST_INIT(dm, obj);
    sdm_res_t *res = &res_1[4];
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[0]->riid, 1);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[1]->riid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_delete, delete_res_middle) {
    TEST_INIT(dm, obj);
    sdm_res_t *res = &res_1[4];
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[0]->riid, 0);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[1]->riid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_delete, delete_res_all) {
    TEST_INIT(dm, obj);
    sdm_res_t *res = &res_1[4];
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[0]->riid, 0);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[1]->riid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[0]->riid, 2);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 2);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 2);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.inst_count, 0);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 3);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_2);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);
}

AVS_UNIT_TEST(sdm_delete, delete_res_error_path) {
    TEST_INIT(dm, obj);
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 1, 1);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path),
            SDM_ERR_NOT_FOUND);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 0);
}

AVS_UNIT_TEST(sdm_delete, delete_res_error_no_instances) {
    TEST_INIT(dm, obj);
    sdm_res_t *res = &res_1[5];
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.inst_count, 0);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_MODIFIED);

    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path),
            SDM_ERR_NOT_FOUND);
}

AVS_UNIT_TEST(sdm_delete, delete_res_error_callback) {
    TEST_INIT(dm, obj);
    res_inst_operation_return_eror = true;
    sdm_res_t *res = &res_1[5];
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, false, &path), -1);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), -1);

    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(res->value.res_inst.insts[0]->riid, 0);

    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_validate, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_delete, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_res_delete, 1);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_0);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_FAILURE);
    res_inst_operation_return_eror = false;
}
#endif // FLUF_WITH_LWM2M12
