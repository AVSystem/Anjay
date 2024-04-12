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

#include "../../../src/anj/sdm/sdm_core.h"

static sdm_obj_t *call_obj;
static sdm_obj_inst_t *call_obj_inst;
static sdm_res_t *call_res;
static sdm_res_inst_t *call_res_inst;
static fluf_op_t call_operation;
static int call_counter_read;
static int call_counter_begin;
static int call_counter_end;
static fluf_res_value_t callback_value;
static sdm_op_result_t call_result;

static int operation_begin(sdm_obj_t *obj, fluf_op_t operation) {
    call_obj = obj;
    call_operation = operation;
    call_counter_begin++;
    return 0;
}
static int operation_end(sdm_obj_t *obj, sdm_op_result_t result) {
    call_obj = obj;
    call_result = result;
    call_counter_end++;
    return 0;
}

static int res_read(sdm_obj_t *obj,
                    sdm_obj_inst_t *obj_inst,
                    sdm_res_t *res,
                    sdm_res_inst_t *res_inst,
                    fluf_res_value_t *out_value) {
    call_obj = obj;
    call_obj_inst = obj_inst;
    call_res = res;
    call_res_inst = res_inst;
    call_counter_read++;
    *out_value = callback_value;
    return 0;
}

static sdm_res_handlers_t res_handlers = {
    .res_read = res_read
};
static const sdm_res_spec_t res_spec_0 = {
    .rid = 0,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_1 = {
    .rid = 1,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_INT
};
// bootstrap read
static const sdm_res_spec_t res_spec_2 = {
    .rid = 2,
    .operation = SDM_RES_BS_RW,
    .type = FLUF_DATA_TYPE_INT
};
// empty multi-instance
static const sdm_res_spec_t res_spec_3 = {
    .rid = 3,
    .operation = SDM_RES_RM,
    .type = FLUF_DATA_TYPE_INT
};
// mulit-instance
static const sdm_res_spec_t res_spec_4 = {
    .rid = 4,
    .operation = SDM_RES_RM,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_5 = {
    .rid = 5,
    .operation = SDM_RES_RM,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_write = {
    .rid = 6,
    .operation = SDM_RES_W,
    .type = FLUF_DATA_TYPE_INT
};
static sdm_res_t res_0[] = {
    {
        .res_spec = &res_spec_0,
        .res_handlers = &res_handlers
    },
    {
        .res_spec = &res_spec_write,
        .res_handlers = &res_handlers
    }
};
static sdm_res_inst_t res_inst_0 = {
    .riid = 0,
    .res_value =
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(33))
};
static sdm_res_inst_t res_inst_1 = {
    .riid = 1,
    .res_value =
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(44))
};
static sdm_res_inst_t *res_insts[9] = { &res_inst_0, &res_inst_1 };
static sdm_res_inst_t *res_insts_2[9] = { &res_inst_0 };
static sdm_res_t res_1[] = {
    {
        .res_spec = &res_spec_0,
        .res_handlers = &res_handlers
    },
    {
        .res_spec = &res_spec_1,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(17))
    },
    {
        .res_spec = &res_spec_2,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(18))
    },
    {
        .res_spec = &res_spec_3,
        .value.res_inst.max_inst_count = 0,
        .value.res_inst.inst_count = 0
    },
    {
        .res_spec = &res_spec_4,
        .value.res_inst.max_inst_count = 9,
        .value.res_inst.inst_count = 2,
        .value.res_inst.insts = res_insts
    },
    {
        .res_spec = &res_spec_5,
        .res_handlers = &res_handlers,
        .value.res_inst.max_inst_count = 9,
        .value.res_inst.inst_count = 1,
        .value.res_inst.insts = res_insts_2
    }
};
static sdm_obj_inst_t obj_inst_0 = {
    .iid = 0,
    .res_count = 2,
    .resources = res_0
};
static sdm_obj_inst_t obj_inst_1 = {
    .iid = 1,
    .res_count = 6,
    .resources = res_1
};
static sdm_obj_inst_t *obj_insts[2] = { &obj_inst_0, &obj_inst_1 };
static sdm_obj_handlers_t handlers = {
    .operation_begin = operation_begin,
    .operation_end = operation_end
};
static sdm_obj_t obj = {
    .oid = 1,
    .insts = obj_insts,
    .inst_count = 2,
    .obj_handlers = &handlers,
    .max_inst_count = 2
};

#define READ_INIT(Dm, Obj)        \
    sdm_data_model_t Dm;          \
    sdm_obj_t *Objs[1];           \
    sdm_initialize(&Dm, Objs, 1); \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&Dm, &Obj));

#define VERIFY_ENTRY(Out, Path, Value)                          \
    AVS_UNIT_ASSERT_TRUE(fluf_uri_path_equal(&Out.path, Path)); \
    AVS_UNIT_ASSERT_EQUAL(Out.value.int_value, Value);          \
    AVS_UNIT_ASSERT_EQUAL(Out.type, FLUF_DATA_TYPE_INT);

AVS_UNIT_TEST(sdm_read, read_res_instance) {
    READ_INIT(dm, obj);
    fluf_io_out_entry_t record = { 0 };
    size_t out_res_count = 0;

    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));

    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(1, out_res_count);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    VERIFY_ENTRY(record, &path, 33);

    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));

    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(1, out_res_count);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    VERIFY_ENTRY(record, &path, 44);

    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0);
    callback_value.int_value = 222;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));

    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(1, out_res_count);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    ;
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    VERIFY_ENTRY(record, &path, 222);

    AVS_UNIT_ASSERT_EQUAL(call_counter_read, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 3);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_NOT_MODIFIED);
    AVS_UNIT_ASSERT_TRUE(call_obj == &obj);
    AVS_UNIT_ASSERT_TRUE(call_obj_inst == &obj_inst_1);
    AVS_UNIT_ASSERT_TRUE(call_res == &res_1[5]);
    AVS_UNIT_ASSERT_TRUE(call_res_inst == &res_inst_0);
}

AVS_UNIT_TEST(sdm_read, read_res_error) {
    READ_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(2, 1, 4, 0);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 2, 4, 0);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 6, 0);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 4);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_RESOURCE_PATH(1, 0, 6);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
}

AVS_UNIT_TEST(sdm_read, empty_read) {
    READ_INIT(dm, obj);
    size_t out_res_count = 0;
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 3);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(0, out_res_count);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
}

AVS_UNIT_TEST(sdm_read, read_res) {
    READ_INIT(dm, obj);
    fluf_io_out_entry_t record = { 0 };
    size_t out_res_count = 0;

    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 4);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(2, out_res_count);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0), 33);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 1), 44);

    callback_value.int_value = 45;
    path = FLUF_MAKE_RESOURCE_PATH(1, 0, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(1, out_res_count);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    VERIFY_ENTRY(record, &path, 45);

    path = FLUF_MAKE_RESOURCE_PATH(1, 1, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(1, out_res_count);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
    VERIFY_ENTRY(record, &path, 17);
}

AVS_UNIT_TEST(sdm_read, read_inst) {
    READ_INIT(dm, obj);
    fluf_io_out_entry_t record = { 0 };
    size_t out_res_count = 0;

    callback_value.int_value = 999;
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(out_res_count, 5);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 0), 999);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 1), 17);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0), 33);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 1), 44);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0), 999);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    callback_value.int_value = 7;
    path = FLUF_MAKE_INSTANCE_PATH(1, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(out_res_count, 1);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 0, 0), 7);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
}

AVS_UNIT_TEST(sdm_read, read_obj) {
    READ_INIT(dm, obj);
    fluf_io_out_entry_t record = { 0 };
    size_t out_res_count = 0;

    call_counter_read = 0;
    call_counter_begin = 0;
    call_counter_end = 0;

    callback_value.int_value = 225;
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(out_res_count, 6);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 0, 0), 225);
    callback_value.int_value = 7;
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 0), 7);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 1), 17);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0), 33);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 1), 44);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0), 7);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(call_counter_read, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_NOT_MODIFIED);
}

AVS_UNIT_TEST(sdm_read, bootstrap_read_obj) {
    READ_INIT(dm, obj);
    fluf_io_out_entry_t record = { 0 };
    size_t out_res_count = 0;

    callback_value.int_value = 225;
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, true, &path));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_readable_res_count(&dm, &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(out_res_count, 7);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 0, 0), 225);
    callback_value.int_value = 7;
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 0), 7);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 1), 17);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 2), 18);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0), 33);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), 0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 1), 44);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_read_entry(&dm, &record), SDM_LAST_RECORD);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0), 7);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
}

AVS_UNIT_TEST(sdm_read, bootstrap_read_obj_error) {
    READ_INIT(dm, obj);

    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(3);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, true, &path),
            SDM_ERR_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_METHOD_NOT_ALLOWED);
    path = FLUF_MAKE_OBJECT_PATH(2);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, true, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_INSTANCE_PATH(1, 2);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, true, &path),
            SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_RESOURCE_PATH(1, 1, 1);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, true, &path),
            SDM_ERR_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_METHOD_NOT_ALLOWED);
    path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_EQUAL(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, true, &path), 0);
}

AVS_UNIT_TEST(sdm_read, get_res_val) {
    READ_INIT(dm, obj);
    fluf_res_value_t out_value;

    callback_value.int_value = 3333;
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    path = FLUF_MAKE_RESOURCE_PATH(1, 0, 0);
    fluf_data_type_t type = 0;
    AVS_UNIT_ASSERT_SUCCESS(
            _sdm_get_resource_value(&dm, &path, &out_value, &type));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_INT);
    AVS_UNIT_ASSERT_EQUAL(out_value.int_value, 3333);
    path = FLUF_MAKE_RESOURCE_PATH(1, 1, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            _sdm_get_resource_value(&dm, &path, &out_value, NULL));
    AVS_UNIT_ASSERT_EQUAL(out_value.int_value, 17);
    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            _sdm_get_resource_value(&dm, &path, &out_value, NULL));
    AVS_UNIT_ASSERT_EQUAL(out_value.int_value, 33);
    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0);
    callback_value.int_value = 3331;
    AVS_UNIT_ASSERT_SUCCESS(
            _sdm_get_resource_value(&dm, &path, &out_value, NULL));
    AVS_UNIT_ASSERT_EQUAL(out_value.int_value, 3331);

    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 2);
    AVS_UNIT_ASSERT_EQUAL(_sdm_get_resource_value(&dm, &path, &out_value, NULL),
                          SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_RESOURCE_PATH(1, 1, 8);
    AVS_UNIT_ASSERT_EQUAL(_sdm_get_resource_value(&dm, &path, &out_value, NULL),
                          SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_EQUAL(_sdm_get_resource_value(&dm, &path, &out_value, NULL),
                          SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_OBJECT_PATH(2);
    AVS_UNIT_ASSERT_EQUAL(_sdm_get_resource_value(&dm, &path, &out_value, NULL),
                          SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_RESOURCE_PATH(1, 1, 5);
    AVS_UNIT_ASSERT_EQUAL(_sdm_get_resource_value(&dm, &path, &out_value, NULL),
                          SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_RESOURCE_PATH(1, 0, 6);
    AVS_UNIT_ASSERT_EQUAL(_sdm_get_resource_value(&dm, &path, &out_value, NULL),
                          SDM_ERR_NOT_FOUND);
}

AVS_UNIT_TEST(sdm_read, get_res_type) {
    READ_INIT(dm, obj);
    fluf_data_type_t out_type = 0;
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ, false, &path));
    path = FLUF_MAKE_RESOURCE_PATH(1, 0, 0);
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_resource_type(&dm, &path, &out_type));
    AVS_UNIT_ASSERT_EQUAL(FLUF_DATA_TYPE_INT, out_type);
    path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0);
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_resource_type(&dm, &path, &out_type));
    AVS_UNIT_ASSERT_EQUAL(FLUF_DATA_TYPE_INT, out_type);

    path = FLUF_MAKE_RESOURCE_PATH(1, 1, 8);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_resource_type(&dm, &path, &out_type),
                          SDM_ERR_NOT_FOUND);
    path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_resource_type(&dm, &path, &out_type),
                          SDM_ERR_INPUT_ARG);
    path = FLUF_MAKE_OBJECT_PATH(2);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_resource_type(&dm, &path, &out_type),
                          SDM_ERR_INPUT_ARG);
}

AVS_UNIT_TEST(sdm_read, composite_read) {
    READ_INIT(dm, obj);
    fluf_io_out_entry_t record = { 0 };
    size_t out_res_count = 0;

    call_counter_begin = 0;
    call_counter_read = 0;
    call_counter_end = 0;
    callback_value.int_value = 755;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_DM_READ_COMP, false, NULL));

    AVS_UNIT_ASSERT_SUCCESS(sdm_get_composite_readable_res_count(
            &dm, &FLUF_MAKE_INSTANCE_PATH(1, 0), &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(out_res_count, 1);
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_composite_readable_res_count(
            &dm, &FLUF_MAKE_INSTANCE_PATH(1, 1), &out_res_count));
    AVS_UNIT_ASSERT_EQUAL(out_res_count, 5);

    AVS_UNIT_ASSERT_EQUAL(sdm_get_composite_read_entry(
                                  &dm, &FLUF_MAKE_INSTANCE_PATH(1, 0), &record),
                          SDM_LAST_RECORD);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 0, 0), 755);
    callback_value.int_value = 7;
    AVS_UNIT_ASSERT_EQUAL(sdm_get_composite_read_entry(
                                  &dm, &FLUF_MAKE_INSTANCE_PATH(1, 1), &record),
                          0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 0), 7);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_composite_read_entry(
                                  &dm, &FLUF_MAKE_INSTANCE_PATH(1, 1), &record),
                          0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_PATH(1, 1, 1), 17);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_composite_read_entry(
                                  &dm, &FLUF_MAKE_INSTANCE_PATH(1, 1), &record),
                          0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 0), 33);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_composite_read_entry(
                                  &dm, &FLUF_MAKE_INSTANCE_PATH(1, 1), &record),
                          0);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 4, 1), 44);
    AVS_UNIT_ASSERT_EQUAL(sdm_get_composite_read_entry(
                                  &dm, &FLUF_MAKE_INSTANCE_PATH(1, 1), &record),
                          SDM_LAST_RECORD);
    VERIFY_ENTRY(record, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(1, 1, 5, 0), 7);
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

    AVS_UNIT_ASSERT_EQUAL(call_counter_read, 3);
    AVS_UNIT_ASSERT_EQUAL(call_counter_begin, 1);
    AVS_UNIT_ASSERT_EQUAL(call_counter_end, 1);
    AVS_UNIT_ASSERT_EQUAL(call_result, SDM_OP_RESULT_SUCCESS_NOT_MODIFIED);
}
