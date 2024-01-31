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

static sdm_obj_t *call_obj;
static sdm_obj_inst_t *call_obj_inst;
static sdm_res_t *call_res;
static size_t call_execute_arg_len;
static const char *call_execute_arg;
static fluf_op_t call_operation;
static int call_counter_execute;
static int call_counter_begin;
static int call_counter_end;
static sdm_op_result_t call_result;

static int operation_begin(sdm_obj_t *obj, fluf_op_t operation) {
    call_obj = obj;
    call_operation = operation;
    call_counter_begin++;
    return 0;
}
static int operation_end(sdm_obj_t *obj, sdm_op_result_t result) {
    call_obj = obj;
    call_counter_end++;
    call_result = result;
    return 0;
}

static int res_execute(sdm_obj_t *obj,
                       sdm_obj_inst_t *obj_inst,
                       sdm_res_t *res,
                       const char *execute_arg,
                       size_t execute_arg_len) {
    call_obj = obj;
    call_obj_inst = obj_inst;
    call_res = res;
    call_execute_arg_len = execute_arg_len;
    call_execute_arg = execute_arg;
    call_counter_execute++;
    return 0;
}

static sdm_res_handlers_t res_handlers = {
    .res_execute = res_execute
};
static const sdm_res_spec_t res_spec_0 = {
    .rid = 0,
    .operation = SDM_RES_E
};
static const sdm_res_spec_t res_spec_1 = {
    .rid = 1,
    .operation = SDM_RES_W,
    .type = FLUF_DATA_TYPE_INT
};
static sdm_res_t res[] = {
    {
        .res_spec = &res_spec_0,
        .res_handlers = &res_handlers
    },
    {
        .res_spec = &res_spec_1
    }
};
static sdm_obj_inst_t obj_inst = {
    .iid = 1,
    .res_count = 2,
    .resources = res
};
static sdm_obj_inst_t *obj_insts[1] = { &obj_inst };
static sdm_obj_handlers_t handlers = {
    .operation_begin = operation_begin,
    .operation_end = operation_end
};
static sdm_obj_t obj = {
    .oid = 1,
    .insts = obj_insts,
    .inst_count = 1,
    .obj_handlers = &handlers,
    .max_inst_count = 1
};

AVS_UNIT_TEST(sdm_execute, base) {
    sdm_data_model_t dm;
    sdm_obj_t *objs[1];
    sdm_initialize(&dm, objs, 1);

    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj));

    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin(
            &dm, FLUF_OP_DM_EXECUTE, false, &FLUF_MAKE_RESOURCE_PATH(1, 1, 0)));

    AVS_UNIT_ASSERT_EQUAL(call_counter_execute, 0);
    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 0);
    AVS_UNIT_ASSERT_TRUE(call_obj == &obj);
    AVS_UNIT_ASSERT_EQUAL(call_operation, FLUF_OP_DM_EXECUTE);

    const char *test_arg = "ddd";
    AVS_UNIT_ASSERT_SUCCESS(sdm_execute(&dm, test_arg, sizeof(test_arg)));
    AVS_UNIT_ASSERT_EQUAL(call_counter_execute, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 0);
    AVS_UNIT_ASSERT_TRUE(call_obj == &obj);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst);
    AVS_UNIT_ASSERT_TRUE(call_res == res);
    AVS_UNIT_ASSERT_TRUE(call_execute_arg == test_arg);
    AVS_UNIT_ASSERT_EQUAL(call_execute_arg_len, sizeof(test_arg));

    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(call_counter_execute, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_NOT_MODIFIED);
}

AVS_UNIT_TEST(sdm_execute, error_calls) {
    sdm_data_model_t dm;
    sdm_obj_t *objs[1];
    sdm_initialize(&dm, objs, 1);

    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj));

    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(1, 1, 1)),
            SDM_ERR_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(1, 2, 1)),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm,
                                FLUF_OP_DM_EXECUTE,
                                false,
                                &FLUF_MAKE_RESOURCE_PATH(2, 2, 1)),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_begin(
            &dm, FLUF_OP_DM_EXECUTE, false, &FLUF_MAKE_RESOURCE_PATH(1, 1, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_LOGIC);
}
