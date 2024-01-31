/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_unit_test.h>

#include <anj/dm.h>

#define OID_5 5 // test object 5
#define OID_6 6 // test object 6
#define OID_7 7 // test object 7

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

static int list_resources_OID_7(dm_t *dm,
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

static const dm_object_def_t DEF_TEST_OBJ_5 = {
    .oid = OID_5,
    .handlers.list_resources = list_resources,
    .handlers.list_instances = dm_list_instances_SINGLE
};
static const dm_object_def_t *const DEF_TEST_OBJ_5_PTR = &DEF_TEST_OBJ_5;

static const dm_object_def_t DEF_TEST_OBJ_6 = {
    .oid = OID_6,
    .handlers.list_resources = list_resources,
    .handlers.list_instances = dm_list_instances_SINGLE
};
static const dm_object_def_t *const DEF_TEST_OBJ_6_PTR = &DEF_TEST_OBJ_6;

const dm_object_def_t DEF_TEST_OBJ_7 = {
    .oid = OID_7,
    .handlers.list_resource_instances = list_resource_instances,
    .handlers.list_resources = list_resources_OID_7,
    .handlers.list_instances = list_instances
};
const dm_object_def_t *const DEF_TEST_OBJ_7_PTR = &DEF_TEST_OBJ_7;

#define OBJ_MAX 4
static dm_t dm;
static dm_installed_object_t objects[OBJ_MAX];

#define SET_UP() dm_initialize(&dm, objects, OBJ_MAX)

AVS_UNIT_TEST(DataModelCoreGetReadable, CoreGetReadResourceNumberRoot) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_5_PTR));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_6_PTR));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_7_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_ROOT_PATH();
    size_t count = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_get_readable_res_count(&dm, &uri, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 16);
}

AVS_UNIT_TEST(DataModelCoreGetReadable, CoreGetReadResourceNumberObject) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_7_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_7);
    size_t count = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_get_readable_res_count(&dm, &uri, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 12);
}

AVS_UNIT_TEST(DataModelCoreGetReadable,
              CoreGetReadResourceNumberObjectInstance) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_7_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_7, 0);
    size_t count = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_get_readable_res_count(&dm, &uri, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 6);
}

AVS_UNIT_TEST(DataModelCoreGetReadable,
              CoreGetReadResourceNumberSingleResource) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_7_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_7, 0, 0);
    size_t count = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_get_readable_res_count(&dm, &uri, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);
}

AVS_UNIT_TEST(DataModelCoreGetReadable,
              CoreGetReadResourceNumberMultipleResource) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_7_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_7, 0, 1);
    size_t count = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_get_readable_res_count(&dm, &uri, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 5);
}

AVS_UNIT_TEST(DataModelCoreGetReadable,
              CoreGetReadResourceNumberResourceInstance) {
    SET_UP();

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_7_PTR));

    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_7, 0, 1, 0);
    size_t count = 0;
    AVS_UNIT_ASSERT_SUCCESS(dm_get_readable_res_count(&dm, &uri, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);
}
