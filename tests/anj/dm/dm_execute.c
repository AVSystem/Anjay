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
#include <anj/dm_io.h>

#include "../../../src/anj/dm/dm_core.h"

#define OID_4 4 // test object

#define IID_0 0

#define RID_11_EXECUTABLE 11
#define RID_12_NON_EXECUTABLE 12
#define RID_13_NOT_PRESENT 13

typedef struct test_object_instance_struct {
    fluf_iid_t iid;
    bool rid_11_execute_flag;
} test_object_instance_t;

typedef struct test_object_struct {
    const dm_object_def_t *def;
    test_object_instance_t instances[1];
} test_object_t;

static int resource_execute(dm_t *dm,
                            const dm_object_def_t *const *obj_ptr,
                            fluf_iid_t iid,
                            fluf_iid_t rid,
                            dm_execute_ctx_t *ctx);
static int list_resources(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          fluf_iid_t iid,
                          dm_resource_list_ctx_t *ctx);

static const dm_object_def_t DEF = {
    .oid = OID_4,
    .handlers.resource_execute = resource_execute,
    .handlers.list_instances = dm_list_instances_SINGLE,
    .handlers.list_resources = list_resources,
};

static test_object_t TEST_OBJECT = {
    .def = &DEF,
    .instances =
            {
                {
                    .iid = IID_0
                }
            }
};

static int resource_execute(dm_t *dm,
                            const dm_object_def_t *const *obj_ptr,
                            fluf_iid_t iid,
                            fluf_iid_t rid,
                            dm_execute_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    (void) rid;
    (void) ctx;
    switch (rid) {
    case RID_11_EXECUTABLE:
        TEST_OBJECT.instances[iid].rid_11_execute_flag = true;
        return 0;
    }
    return -1;
}

static int list_resources(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          fluf_iid_t iid,
                          dm_resource_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    (void) ctx;
    dm_emit_res(ctx, RID_11_EXECUTABLE, DM_RES_E, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_12_NON_EXECUTABLE, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_13_NOT_PRESENT, DM_RES_E, DM_RES_ABSENT);
    return 0;
}

#define OBJ_MAX 3
static dm_t dm;
static dm_installed_object_t objects[OBJ_MAX];

#define SET_UP()                          \
    dm_initialize(&dm, objects, OBJ_MAX); \
    TEST_OBJECT.instances[IID_0].rid_11_execute_flag = false

AVS_UNIT_TEST(DataModelExecute, ExecuteResource) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_11_EXECUTABLE);

    AVS_UNIT_ASSERT_FALSE(TEST_OBJECT.instances[IID_0].rid_11_execute_flag);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_execute(&dm, &uri));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_TRUE(TEST_OBJECT.instances[IID_0].rid_11_execute_flag);
}

AVS_UNIT_TEST(DataModelExecute, ExecuteTryExecuteNotExecutable) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_12_NON_EXECUTABLE);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_execute(&dm, &uri),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}

AVS_UNIT_TEST(DataModelExecute, ExecuteWithRIID) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_INSTANCE_PATH(
            OID_4, IID_0, RID_11_EXECUTABLE, 0);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_execute(&dm, &uri),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}

AVS_UNIT_TEST(DataModelExecute, ExecuteWithNoRID) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_4, IID_0);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_execute(&dm, &uri),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}

AVS_UNIT_TEST(DataModelExecute, ExecuteWithOnlyOID) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_4);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_execute(&dm, &uri),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}

AVS_UNIT_TEST(DataModelExecute, ExecuteResourceWhichDoesNotExist) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_13_NOT_PRESENT);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_execute(&dm, &uri), FLUF_COAP_CODE_NOT_FOUND);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}
