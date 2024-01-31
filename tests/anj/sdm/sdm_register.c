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

static sdm_obj_t obj_0 = {
    .oid = 0
};

static const sdm_res_spec_t res_spec_0 = {
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
static const sdm_res_spec_t res_spec_4 = {
    .rid = 4,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static sdm_res_t inst_1_res[] = {
    {
        .res_spec = &res_spec_0
    },
    {
        .res_spec = &res_spec_1
    }
};
static sdm_res_inst_t res_inst_1 = {
    .riid = 1
};
static sdm_res_inst_t res_inst_2 = {
    .riid = 2
};
static sdm_res_inst_t *res_insts[] = { &res_inst_1, &res_inst_2 };
static sdm_res_t inst_2_res[] = {
    {
        .res_spec = &res_spec_0
    },
    {
        .res_spec = &res_spec_1
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
        .res_spec = &res_spec_4
    }
};
#define OBJ_1_INST_MAX_COUNT 3
static sdm_obj_inst_t obj_1_inst_1 = {
    .iid = 1,
    .res_count = 2,
    .resources = inst_1_res
};
static sdm_obj_inst_t obj_1_inst_2 = {
    .iid = 2,
    .res_count = 5,
    .resources = inst_2_res
};
static sdm_obj_inst_t *obj_1_insts[OBJ_1_INST_MAX_COUNT] = { &obj_1_inst_1,
                                                             &obj_1_inst_2 };
static sdm_obj_t obj_1 = {
    .oid = 1,
    .version = "1.1",
    .insts = obj_1_insts,
    .inst_count = 2,
    .max_inst_count = OBJ_1_INST_MAX_COUNT
};
static sdm_obj_inst_t obj_3_inst_1 = {
    .iid = 0
};
static sdm_obj_inst_t *obj_3_insts[1] = { &obj_3_inst_1 };
static sdm_obj_t obj_3 = {
    .oid = 3,
    .insts = obj_3_insts,
    .inst_count = 1,
    .max_inst_count = 1
};
static sdm_obj_t obj_5 = {
    .oid = 5
};
static sdm_obj_t obj_55 = {
    .oid = 55,
    .version = "1.2"
};

AVS_UNIT_TEST(sdm_register, register_operation) {
    sdm_data_model_t dm;
    sdm_obj_t *objs[5];
    sdm_initialize(&dm, objs, 5);

    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_0));
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_1));
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_3));
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_5));
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_55));

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_operation_begin(&dm, FLUF_OP_REGISTER, false, NULL));

    fluf_uri_path_t path;
    const char *version;
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_register_record(&dm, &path, &version));
    AVS_UNIT_ASSERT_TRUE(fluf_uri_path_equal(&path, &FLUF_MAKE_OBJECT_PATH(1)));
    AVS_UNIT_ASSERT_EQUAL_STRING(version, obj_1.version);
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_register_record(&dm, &path, &version));
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&path, &FLUF_MAKE_INSTANCE_PATH(1, 1)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_register_record(&dm, &path, &version));
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&path, &FLUF_MAKE_INSTANCE_PATH(1, 2)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_register_record(&dm, &path, &version));
    AVS_UNIT_ASSERT_TRUE(fluf_uri_path_equal(&path, &FLUF_MAKE_OBJECT_PATH(3)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_register_record(&dm, &path, &version));
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&path, &FLUF_MAKE_INSTANCE_PATH(3, 0)));
    AVS_UNIT_ASSERT_SUCCESS(sdm_get_register_record(&dm, &path, &version));
    AVS_UNIT_ASSERT_TRUE(fluf_uri_path_equal(&path, &FLUF_MAKE_OBJECT_PATH(5)));
    AVS_UNIT_ASSERT_EQUAL(sdm_get_register_record(&dm, &path, &version),
                          SDM_LAST_RECORD);
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&path, &FLUF_MAKE_OBJECT_PATH(55)));
    AVS_UNIT_ASSERT_EQUAL_STRING(version, obj_55.version);

    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));
}
