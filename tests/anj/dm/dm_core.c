/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

/**
 * @brief Tests for data model Core API
 * @note Note that all strings and values used in this file have
 * no special meaning, they are used only for testing purposes.
 */

#include <avsystem/commons/avs_unit_test.h>

#include <anj/dm.h>

#define OID_1 1
#define OID_2 2
#define OID_3 3
#define OID_4 4

typedef struct test_object_struct {
    const dm_object_def_t *def;
} test_object_t;

static const dm_object_def_t DEF_OID_1 = {
    .oid = OID_1,
};

static const dm_object_def_t DEF_OID_2 = {
    .oid = OID_2,
};

static const dm_object_def_t DEF_OID_3 = {
    .oid = OID_3,
};

static const dm_object_def_t DEF_OID_4 = {
    .oid = OID_4,
};

static const test_object_t TEST_OBJECT_1 = {
    .def = &DEF_OID_1,
};

static const test_object_t TEST_OBJECT_2 = {
    .def = &DEF_OID_2,
};

static const test_object_t TEST_OBJECT_3 = {
    .def = &DEF_OID_3,
};

static const test_object_t TEST_OBJECT_4 = {
    .def = &DEF_OID_4,
};

#define OBJ_MAX 4
static dm_t dm;
static dm_installed_object_t objects[OBJ_MAX];

#define SET_UP() dm_initialize(&dm, objects, OBJ_MAX)

AVS_UNIT_TEST(DataModelCore, RegisterAscendingOrder) {
    SET_UP();
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 0);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_1.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_2.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_3.def));
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[0].def)->oid, OID_1);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[1].def)->oid, OID_2);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[2].def)->oid, OID_3);
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 3);
}

AVS_UNIT_TEST(DataModelCore, RegisterDescendingOrder) {
    SET_UP();
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 0);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_3.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_2.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_1.def));
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[0].def)->oid, OID_1);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[1].def)->oid, OID_2);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[2].def)->oid, OID_3);
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 3);
}

AVS_UNIT_TEST(DataModelCore, RegisterUnordered) {
    SET_UP();
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 0);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_2.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_1.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_3.def));
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[0].def)->oid, OID_1);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[1].def)->oid, OID_2);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[2].def)->oid, OID_3);
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 3);
}

AVS_UNIT_TEST(DataModelCore, RegisterForbidenRegistered) {
    SET_UP();
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 0);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_2.def));
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[0].def)->oid, OID_2);
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 1);
    AVS_UNIT_ASSERT_FAILED(dm_register_object(&dm, &TEST_OBJECT_2.def));
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 1);
}

AVS_UNIT_TEST(DataModelCore, RegisterTooMany) {
    dm_initialize(&dm, objects, 1);
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 0);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_2.def));
    AVS_UNIT_ASSERT_FAILED(dm_register_object(&dm, &TEST_OBJECT_1.def));
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[0].def)->oid, OID_2);
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 1);
}

AVS_UNIT_TEST(DataModelCore, Deregister) {
    SET_UP();
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 0);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_1.def));
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 1);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT_1.def));
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 0);
}

AVS_UNIT_TEST(DataModelCore, DeregisterOneOfMany) {
    SET_UP();
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 0);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_2.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_1.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_3.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT_4.def));
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 4);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT_2.def));
    AVS_UNIT_ASSERT_EQUAL(dm.objects_count, 3);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[0].def)->oid, OID_1);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[1].def)->oid, OID_3);
    AVS_UNIT_ASSERT_EQUAL((*dm.objects[2].def)->oid, OID_4);
}
