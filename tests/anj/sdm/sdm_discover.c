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

typedef struct {
    fluf_uri_path_t path;
    const uint16_t *dim;
    const char *version;
} discover_record_t;

#define DISCOVER_TEST(Path, Idx_start, Idx_end)                           \
    sdm_data_model_t dm;                                                  \
    sdm_obj_t *objs[5];                                                   \
    sdm_initialize(&dm, objs, 5);                                         \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_0));                    \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_1));                    \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_3));                    \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_5));                    \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_55));                   \
    AVS_UNIT_ASSERT_SUCCESS(                                              \
            sdm_operation_begin(&dm, FLUF_OP_DM_DISCOVER, false, &Path)); \
    for (size_t idx = Idx_start; idx <= Idx_end; idx++) {                 \
        fluf_uri_path_t out_path;                                         \
        const char *out_version;                                          \
        const uint16_t *out_dim;                                          \
        int res = sdm_get_discover_record(&dm, &out_path, &out_version,   \
                                          &out_dim);                      \
        AVS_UNIT_ASSERT_TRUE(                                             \
                fluf_uri_path_equal(&out_path, &disc_records[idx].path)); \
        if (disc_records[idx].version) {                                  \
            AVS_UNIT_ASSERT_FALSE(                                        \
                    strcmp(out_version, disc_records[idx].version));      \
        } else {                                                          \
            AVS_UNIT_ASSERT_NULL(out_version);                            \
        }                                                                 \
        AVS_UNIT_ASSERT_TRUE(out_dim == disc_records[idx].dim);           \
        if (idx == Idx_end) {                                             \
            AVS_UNIT_ASSERT_EQUAL(res, SDM_LAST_RECORD);                  \
        } else {                                                          \
            AVS_UNIT_ASSERT_EQUAL(res, 0);                                \
        }                                                                 \
    }                                                                     \
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

/**
 * Object 1:
 * 1: version = "1.1"
 *    1
 *       0
 *       1
 *    2
 *       0
 *       1
 *       2: dim = 2
 *          1
 *          2
 *       3: dim = 0
 *       4
 */
// clang-format off
static discover_record_t disc_records [12] = {
    {.path = FLUF_MAKE_OBJECT_PATH(1), .version = "1.1"},
    {.path = FLUF_MAKE_INSTANCE_PATH(1,1)},
    {.path = FLUF_MAKE_RESOURCE_PATH(1,1,0)},
    {.path = FLUF_MAKE_RESOURCE_PATH(1,1,1)},
    {.path = FLUF_MAKE_INSTANCE_PATH(1,2)},
    {.path = FLUF_MAKE_RESOURCE_PATH(1,2,0)},
    {.path = FLUF_MAKE_RESOURCE_PATH(1,2,1)},
    {.path = FLUF_MAKE_RESOURCE_PATH(1,2,2), .dim = &inst_2_res[2].value.res_inst.inst_count},
    {.path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1,2,2,1)},
    {.path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(1,2,2,2)},
    {.path = FLUF_MAKE_RESOURCE_PATH(1,2,3), .dim = &inst_2_res[3].value.res_inst.inst_count},
    {.path = FLUF_MAKE_RESOURCE_PATH(1,2,4)}
};
// clang-format on
AVS_UNIT_TEST(sdm_discover, discover_operation_object) {
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    DISCOVER_TEST(path, 0, 11);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_1) {
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 1);
    DISCOVER_TEST(path, 1, 3);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_2) {
    fluf_uri_path_t path = FLUF_MAKE_INSTANCE_PATH(1, 2);
    DISCOVER_TEST(path, 4, 11);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_1_res_0) {
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 0);
    DISCOVER_TEST(path, 2, 2);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_1_res_1) {
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 1, 1);
    DISCOVER_TEST(path, 3, 3);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_2_res_0) {
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 2, 0);
    DISCOVER_TEST(path, 5, 5);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_2_res_1) {
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 2, 1);
    DISCOVER_TEST(path, 6, 6);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_2_res_2) {
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 2, 2);
    DISCOVER_TEST(path, 7, 9);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_2_res_3) {
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 2, 3);
    DISCOVER_TEST(path, 10, 10);
}
AVS_UNIT_TEST(sdm_discover, discover_operation_inst_2_res_4) {
    fluf_uri_path_t path = FLUF_MAKE_RESOURCE_PATH(1, 2, 4);
    DISCOVER_TEST(path, 11, 11);
}
