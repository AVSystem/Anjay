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

static const sdm_res_spec_t res_spec_00 = {
    .rid = 0,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_STRING
};
static const sdm_res_spec_t res_spec_01 = {
    .rid = 1,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_BOOL
};
static const sdm_res_spec_t res_spec_010 = {
    .rid = 10,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_017 = {
    .rid = 17,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_OBJLNK
};

static sdm_res_t inst_00_res[] = {
    {
        .res_spec = &res_spec_00,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_STRING("DDD"))
    },
    {
        .res_spec = &res_spec_01,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_BOOL(true))

    },
    {
        .res_spec = &res_spec_010,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_U64(99))
    },
    {
        .res_spec = &res_spec_017,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_OBJLNK(21, 0))
    }
};
static sdm_res_t inst_01_res[] = {
    {
        .res_spec = &res_spec_00,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_STRING("SSS"))
    },
    {
        .res_spec = &res_spec_01,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_BOOL(false))
    },
    {
        .res_spec = &res_spec_010,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_U64(199))
    },
    {
        .res_spec = &res_spec_017,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                0, SDM_INIT_RES_VAL_OBJLNK(21, 0))
    }
};

static sdm_obj_inst_t obj_0_inst_1 = {
    .iid = 0,
    .res_count = 4,
    .resources = inst_00_res
};
static sdm_obj_inst_t obj_0_inst_2 = {
    .iid = 1,
    .res_count = 4,
    .resources = inst_01_res
};
static sdm_obj_inst_t *obj_0_insts[] = { &obj_0_inst_1, &obj_0_inst_2 };
static sdm_obj_t obj_0 = {
    .oid = 0,
    .insts = obj_0_insts,
    .inst_count = 2,
    .max_inst_count = 2
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

static sdm_res_t inst_1_res[] = {
    {
        .res_spec = &res_spec_0,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(11))
    },
    {
        .res_spec = &res_spec_1,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    }
};
static sdm_res_t inst_2_res[] = {
    {
        .res_spec = &res_spec_0,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(22))
    },
    {
        .res_spec = &res_spec_1,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    }
};

static sdm_obj_inst_t obj_1_inst_1 = {
    .iid = 1,
    .res_count = 2,
    .resources = inst_1_res
};
static sdm_obj_inst_t obj_1_inst_2 = {
    .iid = 2,
    .res_count = 2,
    .resources = inst_2_res
};
static sdm_obj_inst_t *obj_1_insts[] = { &obj_1_inst_1, &obj_1_inst_2 };
static sdm_obj_t obj_1 = {
    .oid = 1,
    .version = "1.1",
    .insts = obj_1_insts,
    .inst_count = 2,
    .max_inst_count = 2
};
static sdm_obj_inst_t obj_3_inst_0 = {
    .iid = 0
};
static sdm_obj_inst_t *obj_3_insts[1] = { &obj_3_inst_0 };
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
static sdm_obj_inst_t obj_21_inst_0 = {
    .iid = 0
};
static sdm_obj_inst_t *obj_21_insts[1] = { &obj_21_inst_0 };
static sdm_obj_t obj_21 = {
    .oid = 21,
    .insts = obj_21_insts,
    .inst_count = 1,
    .max_inst_count = 1
};

typedef struct {
    fluf_uri_path_t path;
    const char *version;
    const uint16_t ssid;
    const char *uri;
} boot_discover_record_t;

#define BOOTSTRAP_DISCOVER_TEST(Path, Idx_start, Idx_end)                      \
    sdm_data_model_t dm;                                                       \
    sdm_obj_t *objs[6];                                                        \
    sdm_initialize(&dm, objs, 6);                                              \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_0));                         \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_1));                         \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_3));                         \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_5));                         \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_55));                        \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_21));                        \
    AVS_UNIT_ASSERT_SUCCESS(                                                   \
            sdm_operation_begin(&dm, FLUF_OP_DM_DISCOVER, true, &Path));       \
    for (size_t idx = Idx_start; idx <= Idx_end; idx++) {                      \
        fluf_uri_path_t out_path;                                              \
        const char *out_version;                                               \
        const uint16_t *out_ssid;                                              \
        const char *out_uri;                                                   \
        int res = sdm_get_bootstrap_discover_record(                           \
                &dm, &out_path, &out_version, &out_ssid, &out_uri);            \
        AVS_UNIT_ASSERT_TRUE(                                                  \
                fluf_uri_path_equal(&out_path, &boot_disc_records[idx].path)); \
        if (out_version && boot_disc_records[idx].version) {                   \
            AVS_UNIT_ASSERT_SUCCESS(                                           \
                    strcmp(out_version, boot_disc_records[idx].version));      \
        } else {                                                               \
            AVS_UNIT_ASSERT_TRUE(!out_version                                  \
                                 && !boot_disc_records[idx].version);          \
        }                                                                      \
        if (out_ssid && boot_disc_records[idx].ssid) {                         \
            AVS_UNIT_ASSERT_TRUE(*out_ssid == boot_disc_records[idx].ssid);    \
        } else {                                                               \
            AVS_UNIT_ASSERT_TRUE(!out_ssid && !boot_disc_records[idx].ssid);   \
        }                                                                      \
        if (out_uri && boot_disc_records[idx].uri) {                           \
            AVS_UNIT_ASSERT_SUCCESS(                                           \
                    strcmp(out_uri, boot_disc_records[idx].uri));              \
        } else {                                                               \
            AVS_UNIT_ASSERT_TRUE(!out_uri && !boot_disc_records[idx].uri);     \
        }                                                                      \
        if (idx == Idx_end) {                                                  \
            AVS_UNIT_ASSERT_EQUAL(res, SDM_LAST_RECORD);                       \
        } else {                                                               \
            AVS_UNIT_ASSERT_EQUAL(res, 0);                                     \
        }                                                                      \
    }                                                                          \
    AVS_UNIT_ASSERT_SUCCESS(sdm_operation_end(&dm));

/**
 * 0:
 *    0
 *       0 "DDD"
 *       1 true
 *       10 99
 *       17 21:0
 *    1
 *       0 "SSS"
 *       1 false
 *       10 199
 *       17 21:0
 * 1: version = "1.1"
 *    1
 *       0 SSID = 11
 *       1
 *    2
 *       0 SSID = 22
 *       1
 * 3:
 *    0
 * 5
 * 21:
 *    0
 * 55: version = "1.2"
 */
// clang-format off
static boot_discover_record_t boot_disc_records [12] = {
    {.path = FLUF_MAKE_OBJECT_PATH(0)},
    {.path = FLUF_MAKE_INSTANCE_PATH(0,0)},
    {.path = FLUF_MAKE_INSTANCE_PATH(0,1), .ssid = 199, .uri = "SSS"},
    {.path = FLUF_MAKE_OBJECT_PATH(1), .version = "1.1"},
    {.path = FLUF_MAKE_INSTANCE_PATH(1,1), .ssid = 11},
    {.path = FLUF_MAKE_INSTANCE_PATH(1,2), .ssid = 22},
    {.path = FLUF_MAKE_OBJECT_PATH(3)},
    {.path = FLUF_MAKE_INSTANCE_PATH(3,0)},
    {.path = FLUF_MAKE_OBJECT_PATH(5)},
    {.path = FLUF_MAKE_OBJECT_PATH(21)},
    {.path = FLUF_MAKE_INSTANCE_PATH(21,0), .ssid = 199},
    {.path = FLUF_MAKE_OBJECT_PATH(55), .version = "1.2"}
};
// clang-format on

AVS_UNIT_TEST(sdm_bootstrap_discover, root) {
    fluf_uri_path_t path = FLUF_MAKE_ROOT_PATH();
    BOOTSTRAP_DISCOVER_TEST(path, 0, 11);
}

AVS_UNIT_TEST(sdm_bootstrap_discover, object_0) {
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(0);
    BOOTSTRAP_DISCOVER_TEST(path, 0, 2);
}

AVS_UNIT_TEST(sdm_bootstrap_discover, object_1) {
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(1);
    BOOTSTRAP_DISCOVER_TEST(path, 3, 5);
}

AVS_UNIT_TEST(sdm_bootstrap_discover, object_3) {
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(3);
    BOOTSTRAP_DISCOVER_TEST(path, 6, 7);
}

AVS_UNIT_TEST(sdm_bootstrap_discover, object_5) {
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(5);
    BOOTSTRAP_DISCOVER_TEST(path, 8, 8);
}

AVS_UNIT_TEST(sdm_bootstrap_discover, object_21) {
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(21);
    BOOTSTRAP_DISCOVER_TEST(path, 9, 10);
}

AVS_UNIT_TEST(sdm_bootstrap_discover, object_55) {
    fluf_uri_path_t path = FLUF_MAKE_OBJECT_PATH(55);
    BOOTSTRAP_DISCOVER_TEST(path, 11, 11);
}
