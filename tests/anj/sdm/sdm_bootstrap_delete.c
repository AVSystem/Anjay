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

// sdm deletes instance but handler must be defined
static int inst_delete(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst) {
    (void) obj;
    (void) obj_inst;
    return 0;
}
static const sdm_obj_handlers_t handlers = {
    .inst_delete = inst_delete
};
static const sdm_res_spec_t res_spec_01 = {
    .rid = 1,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_BOOL
};
static const sdm_res_spec_t res_spec_017 = {
    .rid = 17,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_OBJLNK
};
static sdm_res_t inst_00_res[] = {
    {
        .res_spec = &res_spec_01,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_BOOL(false))
    },
    {
        .res_spec = &res_spec_017,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_OBJLNK(21, 0))
    }
};
static sdm_res_t inst_01_res[] = {
    {
        .res_spec = &res_spec_01,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_BOOL(true))
    },
    {
        .res_spec = &res_spec_017,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_OBJLNK(21, 1))
    }
};
static const sdm_res_spec_t res_spec_0 = {
    .rid = 0,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static sdm_res_t obj_1_inst_1_res[] = {
    {
        .res_spec = &res_spec_0,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(11))
    }
};
#define DELETE_TEST_INIT()                                                   \
    static sdm_obj_inst_t obj_0_inst_0 = {                                   \
        .iid = 0,                                                            \
        .res_count = 2,                                                      \
        .resources = inst_00_res                                             \
    };                                                                       \
    static sdm_obj_inst_t obj_0_inst_1 = {                                   \
        .iid = 1,                                                            \
        .res_count = 2,                                                      \
        .resources = inst_01_res                                             \
    };                                                                       \
    static sdm_obj_inst_t *obj_0_insts[] = { &obj_0_inst_0, &obj_0_inst_1 }; \
    static sdm_obj_t obj_0 = {                                               \
        .oid = 0,                                                            \
        .insts = obj_0_insts,                                                \
        .inst_count = 2,                                                     \
        .max_inst_count = 2,                                                 \
        .obj_handlers = &handlers                                            \
    };                                                                       \
    static sdm_obj_inst_t obj_1_inst_1 = {                                   \
        .iid = 1,                                                            \
        .res_count = 1,                                                      \
        .resources = obj_1_inst_1_res                                        \
    };                                                                       \
    static sdm_obj_inst_t *obj_1_insts[] = { &obj_1_inst_1 };                \
    static sdm_obj_t obj_1 = {                                               \
        .oid = 1,                                                            \
        .insts = obj_1_insts,                                                \
        .inst_count = 1,                                                     \
        .max_inst_count = 1,                                                 \
        .obj_handlers = &handlers                                            \
    };                                                                       \
    static sdm_obj_inst_t obj_3_inst_0 = {                                   \
        .iid = 0                                                             \
    };                                                                       \
    static sdm_obj_inst_t *obj_3_insts[] = { &obj_3_inst_0 };                \
    static sdm_obj_t obj_3 = {                                               \
        .oid = 3,                                                            \
        .insts = obj_3_insts,                                                \
        .inst_count = 1,                                                     \
        .max_inst_count = 1,                                                 \
        .obj_handlers = &handlers                                            \
    };                                                                       \
    static sdm_obj_inst_t obj_21_inst_0 = {                                  \
        .iid = 0                                                             \
    };                                                                       \
    static sdm_obj_inst_t obj_21_inst_1 = {                                  \
        .iid = 1                                                             \
    };                                                                       \
    static sdm_obj_inst_t *obj_21_insts[] = { &obj_21_inst_0,                \
                                              &obj_21_inst_1 };              \
    static sdm_obj_t obj_21 = {                                              \
        .oid = 21,                                                           \
        .insts = obj_21_insts,                                               \
        .inst_count = 2,                                                     \
        .max_inst_count = 2,                                                 \
        .obj_handlers = &handlers                                            \
    };                                                                       \
    sdm_data_model_t dm;                                                     \
    sdm_obj_t *objs[4];                                                      \
    sdm_initialize(&dm, objs, 4);                                            \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_0));                       \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_1));                       \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_3));                       \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_21));

#define DELETE_TEST(Path)                                                \
    AVS_UNIT_ASSERT_SUCCESS(                                             \
            sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, true, &(Path))); \
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

AVS_UNIT_TEST(sdm_bootstrap_delete, root) {
    DELETE_TEST_INIT();
    DELETE_TEST(FLUF_MAKE_ROOT_PATH());
    // all instances should be deleted except for the bootstrap server instance,
    // related OSCORE instance and device object instance
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(obj_0.insts[0] == &obj_0_inst_1);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 0);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(obj_21.insts[0] == &obj_21_inst_1);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, root_swap_instance_order) {
    DELETE_TEST_INIT();
    inst_00_res[0].value.res_value->value.bool_value = true;
    inst_01_res[0].value.res_value->value.bool_value = false;
    DELETE_TEST(FLUF_MAKE_ROOT_PATH());
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(obj_0.insts[0] == &obj_0_inst_0);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 0);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(obj_21.insts[0] == &obj_21_inst_0);
    inst_00_res[0].value.res_value->value.bool_value = false;
    inst_01_res[0].value.res_value->value.bool_value = true;
}

AVS_UNIT_TEST(sdm_bootstrap_delete, security_instance_0) {
    DELETE_TEST_INIT();
    DELETE_TEST(FLUF_MAKE_INSTANCE_PATH(0, 0));
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(obj_0.insts[0] == &obj_0_inst_1);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 2);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, security_instance_1) {
    DELETE_TEST_INIT();
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, true,
                                              &FLUF_MAKE_INSTANCE_PATH(0, 1)),
                          SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 2);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, security_obj) {
    DELETE_TEST_INIT();
    DELETE_TEST(FLUF_MAKE_OBJECT_PATH(0));
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(obj_0.insts[0] == &obj_0_inst_1);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 2);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, server_instance) {
    DELETE_TEST_INIT();
    DELETE_TEST(FLUF_MAKE_INSTANCE_PATH(1, 1));
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 0);
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 2);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, server_obj) {
    DELETE_TEST_INIT();
    DELETE_TEST(FLUF_MAKE_OBJECT_PATH(1));
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 0);
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 2);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, device_obj) {
    DELETE_TEST_INIT();
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, true,
                                              &FLUF_MAKE_OBJECT_PATH(3)),
                          SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 2);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, device_instance) {
    DELETE_TEST_INIT();
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, true,
                                              &FLUF_MAKE_INSTANCE_PATH(3, 0)),
                          SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 2);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, oscore_obj) {
    DELETE_TEST_INIT();
    DELETE_TEST(FLUF_MAKE_OBJECT_PATH(21));
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(obj_21.insts[0] == &obj_21_inst_1);
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, oscore_instance_0) {
    DELETE_TEST_INIT();
    DELETE_TEST(FLUF_MAKE_INSTANCE_PATH(21, 0));
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 1);
    AVS_UNIT_ASSERT_TRUE(obj_21.insts[0] == &obj_21_inst_1);
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
}

AVS_UNIT_TEST(sdm_bootstrap_delete, oscore_instance_1) {
    DELETE_TEST_INIT();
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_begin(&dm, FLUF_OP_DM_DELETE, true,
                                              &FLUF_MAKE_INSTANCE_PATH(0, 1)),
                          SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(sdm_operation_end(&dm), SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(obj_21.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_0.inst_count, 2);
    AVS_UNIT_ASSERT_EQUAL(obj_1.inst_count, 1);
    AVS_UNIT_ASSERT_EQUAL(obj_3.inst_count, 1);
}
