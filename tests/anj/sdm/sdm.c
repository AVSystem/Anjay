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

AVS_UNIT_TEST(sdm, add_remove_object) {
    sdm_data_model_t dm_obj_test;
    sdm_obj_t *objs_array[5];
    uint16_t objs_array_size = 5;

    sdm_initialize(&dm_obj_test, objs_array, objs_array_size);
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.max_allowed_objs_number, 5);

    sdm_obj_t obj_1 = {
        .oid = 1
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm_obj_test, &obj_1));
    sdm_obj_t obj_2 = {
        .oid = 3,
        .version = "2.2"
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm_obj_test, &obj_2));
    sdm_obj_t obj_3 = {
        .oid = 2
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm_obj_test, &obj_3));
    sdm_obj_t obj_e1 = {
        .oid = 2
    };
    AVS_UNIT_ASSERT_EQUAL(sdm_add_obj(&dm_obj_test, &obj_e1), SDM_ERR_LOGIC);
    sdm_obj_t obj_4 = {
        .oid = 0
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm_obj_test, &obj_4));
    sdm_obj_t obj_5 = {
        .oid = 4
    };
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm_obj_test, &obj_5));
    sdm_obj_t obj_e3 = {
        .oid = 7
    };
    AVS_UNIT_ASSERT_EQUAL(sdm_add_obj(&dm_obj_test, &obj_e3), SDM_ERR_MEMORY);
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 5);

    AVS_UNIT_ASSERT_SUCCESS(sdm_remove_obj(&dm_obj_test, 4));
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 4);
    AVS_UNIT_ASSERT_EQUAL(sdm_remove_obj(&dm_obj_test, 4), SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 4);
    AVS_UNIT_ASSERT_SUCCESS(sdm_remove_obj(&dm_obj_test, 1));
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 3);
    AVS_UNIT_ASSERT_SUCCESS(sdm_remove_obj(&dm_obj_test, 2));
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 2);
    AVS_UNIT_ASSERT_SUCCESS(sdm_remove_obj(&dm_obj_test, 3));
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 1);
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm_obj_test, &obj_3));
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 2);
    AVS_UNIT_ASSERT_SUCCESS(sdm_remove_obj(&dm_obj_test, 2));
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 1);
    AVS_UNIT_ASSERT_SUCCESS(sdm_remove_obj(&dm_obj_test, 0));
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 0);
    AVS_UNIT_ASSERT_EQUAL(sdm_remove_obj(&dm_obj_test, 4), SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(dm_obj_test.objs_count, 0);
}

static sdm_res_spec_t res_spec_0 = {
    .rid = 0,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_1 = {
    .rid = 1,
    .operation = SDM_RES_W,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_2 = {
    .rid = 2,
    .operation = SDM_RES_RWM,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_3 = {
    .rid = 3,
    .operation = SDM_RES_WM,
    .type = FLUF_DATA_TYPE_INT
};
static sdm_res_spec_t res_spec_4 = {
    .rid = 4,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_5 = {
    .rid = 5,
    .operation = SDM_RES_E
};
static sdm_res_t inst_1_res[] = {
    {
        .res_spec = &res_spec_0,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
    {
        .res_spec = &res_spec_1,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    }
};
static sdm_res_inst_t res_inst_1 = {
    .riid = 1,
    .res_value = &SDM_MAKE_RES_VALUE(0)
};
static sdm_res_inst_t res_inst_2 = {
    .riid = 2,
    .res_value = &SDM_MAKE_RES_VALUE(0)
};
static sdm_res_inst_t *res_insts[] = { &res_inst_1, &res_inst_2 };
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
static sdm_res_t inst_2_res[] = {
    {
        .res_spec = &res_spec_0,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
    {
        .res_spec = &res_spec_1,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
    {
        .res_spec = &res_spec_2,
        .value.res_inst.inst_count = 2,
        .value.res_inst.max_inst_count = 2,
        .value.res_inst.insts = res_insts
    },
    {
        .res_spec = &res_spec_3,
        .value.res_inst.inst_count = 0
    },
    {
        .res_spec = &res_spec_4,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
    {
        .res_spec = &res_spec_5,
        .res_handlers = &res_handlers
    }
};
static sdm_obj_inst_t obj_1_inst_1 = {
    .iid = 1,
    .res_count = 2,
    .resources = inst_1_res
};
static sdm_obj_inst_t obj_1_inst_2 = {
    .iid = 2,
    .res_count = 6,
    .resources = inst_2_res
};
static sdm_obj_inst_t *obj_1_insts[2] = { &obj_1_inst_1, &obj_1_inst_2 };
static sdm_obj_t obj = {
    .oid = 1,
    .version = "1.1",
    .insts = obj_1_insts,
    .inst_count = 2,
    .max_inst_count = 2
};

AVS_UNIT_TEST(sdm, add_obj_check) {
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_instances) {
    obj.insts = NULL;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    obj.insts = obj_1_insts;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_max_inst_count) {
    obj.max_inst_count = 1;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    obj.max_inst_count = 2;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_iid) {
    obj.insts[0]->iid = 5;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    obj.insts[0]->iid = 2;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    obj.insts[0]->iid = 1;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_rid) {
    res_spec_0.rid = 5;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    res_spec_0.rid = 0;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_type) {
    res_spec_4.type = 7777;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    res_spec_4.type = FLUF_DATA_TYPE_INT;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_riid) {
    res_inst_1.riid = 2;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    res_inst_1.riid = 1;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_execute_handler) {
    res_handlers.res_execute = NULL;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    res_handlers.res_execute = res_execute;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_execute_handler_2) {
    inst_2_res[5].res_handlers = NULL;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    inst_2_res[5].res_handlers = &res_handlers;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}

AVS_UNIT_TEST(sdm, add_obj_check_error_max_allowed_res_insts_number) {
    inst_2_res[2].value.res_inst.max_inst_count = 1;
    AVS_UNIT_ASSERT_EQUAL(_sdm_check_obj(&obj), SDM_ERR_INPUT_ARG);
    inst_2_res[2].value.res_inst.max_inst_count = 2;
    AVS_UNIT_ASSERT_SUCCESS(_sdm_check_obj(&obj));
}
