/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

/**
 * @brief Tests for Data Model Core API
 * @note Note that all strings and values read from Data Model in this file have
 * no special meaning, they are used only for testing purposes.
 */
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <anj/dm.h>
#include <anj/dm_io.h>

#include "../../../src/anj/dm_core.h"

#define OID_4 4 // test object 4
#define OID_5 5 // test object 5
#define OID_6 6 // test object 6

static int list_instances(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          dm_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    dm_emit(ctx, 0);
    dm_emit(ctx, 1);
    return 0;
}

static int list_resources(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          fluf_iid_t iid,
                          dm_resource_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    dm_emit_res(ctx, 0, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, 1, DM_RES_R, DM_RES_PRESENT);
    return 0;
}

static int list_resources_OID_6(dm_t *dm,
                                const dm_object_def_t *const *obj_ptr,
                                fluf_iid_t iid,
                                dm_resource_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    dm_emit_res(ctx, 0, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, 1, DM_RES_RM, DM_RES_PRESENT);
    dm_emit_res(ctx, 2, DM_RES_E, DM_RES_PRESENT);
    return 0;
}

static int list_resource_instances(dm_t *dm,
                                   const dm_object_def_t *const *obj_ptr,
                                   fluf_iid_t iid,
                                   fluf_rid_t rid,
                                   dm_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;

    switch (rid) {
    case 1: {
        for (fluf_riid_t i = 0; i < 5; ++i) {
            dm_emit(ctx, i);
        }
        return 0;
    }
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }
}

static const dm_object_def_t DEF_TEST_OBJ_4 = {
    .oid = OID_4,
    .handlers.list_resources = list_resources,
    .handlers.list_instances = dm_list_instances_SINGLE
};
static const dm_object_def_t *const DEF_TEST_OBJ_4_PTR = &DEF_TEST_OBJ_4;

static const dm_object_def_t DEF_TEST_OBJ_5 = {
    .oid = OID_5,
    .handlers.list_resources = list_resources,
    .handlers.list_instances = dm_list_instances_SINGLE
};
static const dm_object_def_t *const DEF_TEST_OBJ_5_PTR = &DEF_TEST_OBJ_5;

const dm_object_def_t DEF_TEST_OBJ_6 = {
    .oid = OID_6,
    .handlers.list_resource_instances = list_resource_instances,
    .handlers.list_resources = list_resources_OID_6,
    .handlers.list_instances = list_instances
};
const dm_object_def_t *const DEF_TEST_OBJ_6_PTR = &DEF_TEST_OBJ_6;

#define TEST_BUFF_LEN 20
static struct test_buffer {
    fluf_uri_path_t buff[TEST_BUFF_LEN];
    size_t buff_len;
} g_test_buffer;

static int register_clb(void *arg, fluf_uri_path_t *uri) {
    struct test_buffer *buff = (struct test_buffer *) arg;
    buff->buff[buff->buff_len++] = *uri;
    return 0;
}

static int discover_clb(void *arg, fluf_uri_path_t *uri) {
    struct test_buffer *buff = (struct test_buffer *) arg;
    buff->buff[buff->buff_len++] = *uri;
    return 0;
}

#define OBJ_MAX 4
static dm_t dm;
static dm_installed_object_t objects[OBJ_MAX];

#define SET_UP()                                         \
    g_test_buffer = (struct test_buffer) { 0 };          \
    memset(&g_test_buffer, 0x00, sizeof(g_test_buffer)); \
    dm_initialize(&dm, objects, OBJ_MAX)

static void fluf_uri_path_t_compare(const fluf_uri_path_t *a,
                                    const fluf_uri_path_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->uri_len, b->uri_len);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(a->ids, b->ids, FLUF_URI_PATH_MAX_LENGTH);
}

AVS_UNIT_TEST(DataModelRegisterDiscover, QueryDMForRegister) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_4_PTR));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_5_PTR));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    dm_register_ctx_t ctx = {
        .callback = register_clb,
        .arg = &g_test_buffer
    };
    AVS_UNIT_ASSERT_SUCCESS(dm_register_prepare(&dm, &ctx));
    fluf_uri_path_t expected[8] = {
        [0] = FLUF_MAKE_OBJECT_PATH(OID_4),
        [1] = FLUF_MAKE_INSTANCE_PATH(OID_4, 0),
        [2] = FLUF_MAKE_OBJECT_PATH(OID_5),
        [3] = FLUF_MAKE_INSTANCE_PATH(OID_5, 0),
        [4] = FLUF_MAKE_OBJECT_PATH(OID_6),
        [5] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
        [6] = FLUF_MAKE_INSTANCE_PATH(OID_6, 1),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 7);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover, CorePrepareDiscoverObject) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_6);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, NULL, &ctx));

    fluf_uri_path_t expected[10] = {
        [0] = FLUF_MAKE_OBJECT_PATH(OID_6),
        [1] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
        [2] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
        [3] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
        [4] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 2),
        [5] = FLUF_MAKE_INSTANCE_PATH(OID_6, 1),
        [6] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 0),
        [7] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 1),
        [8] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 2),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 9);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverObjectWithDepthZero) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_6);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[3] = {
        [0] = FLUF_MAKE_OBJECT_PATH(OID_6),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 1);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverObjectWithDepthOne) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_6);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 1;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[5] = {
        [0] = FLUF_MAKE_OBJECT_PATH(OID_6),
        [1] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
        [2] = FLUF_MAKE_INSTANCE_PATH(OID_6, 1),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 3);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverObjectWithDepthTwo) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_6);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 2;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[10] = {
        [0] = FLUF_MAKE_OBJECT_PATH(OID_6),
        [1] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
        [2] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
        [3] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
        [4] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 2),
        [5] = FLUF_MAKE_INSTANCE_PATH(OID_6, 1),
        [6] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 0),
        [7] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 1),
        [8] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 2),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 9);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverObjectWithDepthThree) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_6);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 3;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[20] = {
        [0] = FLUF_MAKE_OBJECT_PATH(OID_6),
        [1] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
        [2] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
        [3] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
        [4] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 0),
        [5] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 1),
        [6] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 2),
        [7] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 3),
        [8] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 4),
        [9] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 2),
        [10] = FLUF_MAKE_INSTANCE_PATH(OID_6, 1),
        [11] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 0),
        [12] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 1),
        [13] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 1, 1, 0),
        [14] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 1, 1, 1),
        [15] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 1, 1, 2),
        [16] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 1, 1, 3),
        [17] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 1, 1, 4),
        [18] = FLUF_MAKE_RESOURCE_PATH(OID_6, 1, 2),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 19);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover, CorePrepareDiscoverObjectInstance) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_6, 0);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, NULL, &ctx));

    fluf_uri_path_t expected[5] = {
        [0] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
        [1] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
        [2] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
        [3] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 2),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 4);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverObjectInstanceDepthZero) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_6, 0);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[5] = {
        [0] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 1);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverObjectInstanceDepthOne) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_6, 0);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 1;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[5] = {
        [0] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
        [1] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
        [2] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
        [3] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 2),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 4);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverObjectInstanceDepthTwo) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_6, 0);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 2;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[10] = {
        [0] = FLUF_MAKE_INSTANCE_PATH(OID_6, 0),
        [1] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
        [2] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
        [3] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 0),
        [4] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 1),
        [5] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 2),
        [6] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 3),
        [7] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 4),
        [8] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 2),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 9);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverSingleInstanceResource) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, NULL, &ctx));

    fluf_uri_path_t expected[2] = {
        [0] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 1);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverSingleInstanceResourceDepthZero) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[2] = {
        [0] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 1);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverSingleInstanceResourceDepthOne) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 1;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[2] = {
        [0] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 0),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 1);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverMultiInstanceResource) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, NULL, &ctx));

    fluf_uri_path_t expected[7] = {
        [0] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
        [1] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 0),
        [2] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 1),
        [3] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 2),
        [4] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 3),
        [5] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 4),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 6);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverMultiInstanceResourceDepthZero) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[2] = {
        [0] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 1);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover,
              CorePrepareDiscoverMultiInstanceResourceDepthOne) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    uint8_t depth = 1;
    AVS_UNIT_ASSERT_SUCCESS(dm_discover_resp_prepare(&dm, &uri, &depth, &ctx));

    fluf_uri_path_t expected[7] = {
        [0] = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 1),
        [1] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 0),
        [2] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 1),
        [3] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 2),
        [4] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 3),
        [5] = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_6, 0, 1, 4),
    };
    AVS_UNIT_ASSERT_EQUAL(g_test_buffer.buff_len, 6);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(expected); ++i) {
        fluf_uri_path_t_compare(&g_test_buffer.buff[i], &expected[i]);
    }
}

AVS_UNIT_TEST(DataModelRegisterDiscover, TryDiscoverNotRegisteredObject) {
    SET_UP();

    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_6);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    AVS_UNIT_ASSERT_EQUAL(dm_discover_resp_prepare(&dm, &uri, NULL, &ctx),
                          FLUF_COAP_CODE_NOT_FOUND);
}

AVS_UNIT_TEST(DataModelRegisterDiscover, TryDiscoverNotExistingInstance) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_6, 2137);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    AVS_UNIT_ASSERT_EQUAL(dm_discover_resp_prepare(&dm, &uri, NULL, &ctx),
                          FLUF_COAP_CODE_NOT_FOUND);
}

AVS_UNIT_TEST(DataModelRegisterDiscover, TryDiscoverNotExistingResource) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_6, 0, 2137);
    dm_discover_ctx_t ctx = {
        .callback = discover_clb,
        .arg = &g_test_buffer
    };
    AVS_UNIT_ASSERT_EQUAL(dm_discover_resp_prepare(&dm, &uri, NULL, &ctx),
                          FLUF_COAP_CODE_NOT_FOUND);
}
