/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#include <avsystem/commons/avs_unit_test.h>

#include "src/core/anjay_io_core.h"
#include "tests/utils/dm.h"

#define LWM2M_GATEWAY_TESTS_INIT()                                         \
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER);               \
    AVS_UNIT_ASSERT_SUCCESS(anjay_lwm2m_gateway_install(anjay));           \
    anjay_iid_t iid = ANJAY_ID_INVALID;                                    \
    lwm2m_gateway_instance_t *inst;                                        \
    lwm2m_gateway_obj_t *gw;                                               \
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);                               \
    gw = (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay_unlocked,  \
                                                          gateway_delete); \
    (void) gw;                                                             \
    (void) inst;                                                           \
    (void) iid;                                                            \
    ANJAY_MUTEX_UNLOCK(anjay);

AVS_UNIT_TEST(lwm2m_gateway, add_and_remove_instances) {
    LWM2M_GATEWAY_TESTS_INIT();

    // register 1st device
    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 0);
    inst = find_instance(gw, iid);
    AVS_UNIT_ASSERT_NOT_NULL(inst);
    AVS_UNIT_ASSERT_EQUAL_STRING(inst->prefix, "dev0");

    // register 2nd device
    iid = 1;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN02", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    inst = find_instance(gw, iid);
    AVS_UNIT_ASSERT_NOT_NULL(inst);
    AVS_UNIT_ASSERT_EQUAL_STRING(inst->prefix, "dev1");

    // register 3rd device
    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN02", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    inst = find_instance(gw, iid);
    AVS_UNIT_ASSERT_NOT_NULL(inst);
    AVS_UNIT_ASSERT_EQUAL_STRING(inst->prefix, "dev2");

    // try registering 3rd device again
    iid = 2;
    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_register_device(anjay, "SN02", &iid));
    inst = find_instance(gw, iid);
    AVS_UNIT_ASSERT_NOT_NULL(inst);
    AVS_UNIT_ASSERT_EQUAL_STRING(inst->prefix, "dev2");

    // remove 2nd device
    AVS_UNIT_ASSERT_SUCCESS(anjay_lwm2m_gateway_deregister_device(anjay, 1));
    AVS_UNIT_ASSERT_NOT_NULL(find_instance(gw, 0));
    AVS_UNIT_ASSERT_NULL(find_instance(gw, 1));
    AVS_UNIT_ASSERT_NOT_NULL(find_instance(gw, 2));

    // add 4th device that gets first free iid - 1
    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);

    // remove 1st device
    AVS_UNIT_ASSERT_SUCCESS(anjay_lwm2m_gateway_deregister_device(anjay, 0));
    AVS_UNIT_ASSERT_NULL(find_instance(gw, 0));
    AVS_UNIT_ASSERT_NOT_NULL(find_instance(gw, 1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, register_and_deregister_before_installing) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER);

    anjay_iid_t iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_FAILED(anjay_lwm2m_gateway_deregister_device(anjay, 0));
    AVS_UNIT_ASSERT_FAILED(anjay_lwm2m_gateway_deregister_device(anjay, 1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, deregister_non_existent) {
    LWM2M_GATEWAY_TESTS_INIT();

    AVS_UNIT_ASSERT_FAILED(anjay_lwm2m_gateway_deregister_device(anjay, 0));
    AVS_UNIT_ASSERT_FAILED(anjay_lwm2m_gateway_deregister_device(anjay, 1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, install_twice) {
    LWM2M_GATEWAY_TESTS_INIT();

    AVS_UNIT_ASSERT_FAILED(anjay_lwm2m_gateway_install(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, map_prefix_not_installed) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER);
    anjay_dm_t dummy_dm;
    const anjay_dm_t *dm = &dummy_dm;
    anjay_attr_storage_t dummy_as;
    anjay_attr_storage_t *as = &dummy_as;

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_FAILED(
            _anjay_lwm2m_gateway_prefix_to_dm(anjay_unlocked, "dev0", &dm));
    AVS_UNIT_ASSERT_FAILED(
            _anjay_lwm2m_gateway_prefix_to_as(anjay_unlocked, "dev0", &as));
    ANJAY_MUTEX_UNLOCK(anjay);
    AVS_UNIT_ASSERT_NULL(dm);
    AVS_UNIT_ASSERT_NULL(as);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, map_prefix_found) {
    LWM2M_GATEWAY_TESTS_INIT();
    const anjay_dm_t *dm = NULL;
    anjay_attr_storage_t *as = NULL;

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_lwm2m_gateway_prefix_to_dm(anjay_unlocked, "dev0", &dm));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_lwm2m_gateway_prefix_to_as(anjay_unlocked, "dev0", &as));
    ANJAY_MUTEX_UNLOCK(anjay);
    AVS_UNIT_ASSERT_NOT_NULL(dm);
    AVS_UNIT_ASSERT_NOT_NULL(as);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, map_prefix_not_found) {
    LWM2M_GATEWAY_TESTS_INIT();
    anjay_dm_t dummy_dm;
    const anjay_dm_t *dm = &dummy_dm;
    anjay_attr_storage_t dummy_as;
    anjay_attr_storage_t *as = &dummy_as;

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_FAILED(
            _anjay_lwm2m_gateway_prefix_to_dm(anjay_unlocked, "prefix", &dm));
    AVS_UNIT_ASSERT_FAILED(
            _anjay_lwm2m_gateway_prefix_to_as(anjay_unlocked, "prefix", &as));
    ANJAY_MUTEX_UNLOCK(anjay);
    AVS_UNIT_ASSERT_NULL(dm);
    AVS_UNIT_ASSERT_NULL(as);

    DM_TEST_FINISH;
}

static anjay_dm_object_def_t *make_mock_object(anjay_oid_t oid) {
    anjay_dm_object_def_t *obj =
            (anjay_dm_object_def_t *) avs_calloc(1,
                                                 sizeof(anjay_dm_object_def_t));
    if (obj) {
        obj->oid = oid;
    }
    return obj;
}

AVS_UNIT_TEST(lwm2m_gateway, register_objects_ok) {
    LWM2M_GATEWAY_TESTS_INIT();

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));

    const anjay_dm_object_def_t *mock_obj1 = make_mock_object(32);
    const anjay_dm_object_def_t *mock_obj2 = make_mock_object(23);

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj1));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj2));

    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj1);
    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj2);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, register_object_gateway_not_installed) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER);
    anjay_iid_t iid = 1;

    const anjay_dm_object_def_t *mock_obj1 = make_mock_object(32);

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj1));

    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj1);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, register_object_device_not_found) {
    LWM2M_GATEWAY_TESTS_INIT();

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    iid += 1;

    const anjay_dm_object_def_t *mock_obj1 = make_mock_object(32);

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj1));

    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj1);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, register_object_invalid_obj_def) {
    LWM2M_GATEWAY_TESTS_INIT();

    const anjay_dm_object_def_t *mock_obj1 = NULL;

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, unregister_objects_ok) {
    LWM2M_GATEWAY_TESTS_INIT();

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));

    const anjay_dm_object_def_t *mock_obj1 = make_mock_object(32);
    const anjay_dm_object_def_t *mock_obj2 = make_mock_object(23);

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj1));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj2));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_unregister_object(anjay, iid, &mock_obj1));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_unregister_object(anjay, iid, &mock_obj2));

    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj1);
    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj2);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, unregister_object_gateway_not_installed) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER);
    anjay_iid_t iid = 1;

    const anjay_dm_object_def_t *mock_obj1 = make_mock_object(32);

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj1));

    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj1);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, unregister_object_device_not_found) {
    LWM2M_GATEWAY_TESTS_INIT();

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    iid += 1;

    const anjay_dm_object_def_t *mock_obj1 = NULL;

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_unregister_object(anjay, iid, &mock_obj1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, unregister_object_invalid_obj_def) {
    LWM2M_GATEWAY_TESTS_INIT();

    const anjay_dm_object_def_t *mock_obj1 = NULL;

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_unregister_object(anjay, iid, &mock_obj1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, unregister_not_installed) {
    LWM2M_GATEWAY_TESTS_INIT();

    const anjay_dm_object_def_t *mock_obj1 = make_mock_object(32);

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_unregister_object(anjay, iid, &mock_obj1));

    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj1);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, notify_changed_success) {
    LWM2M_GATEWAY_TESTS_INIT();

    // Register device
    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 0);

    // Notify a resource change
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_notify_changed(anjay, iid, 3, 0, 1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, notify_changed_gateway_not_installed) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER);

    // Attempt to notify without installing the gateway
    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_notify_changed(anjay, 0, 3, 0, 1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, notify_changed_device_not_registered) {
    LWM2M_GATEWAY_TESTS_INIT();

    // Attempt to notify a change for an unregistered device
    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_notify_changed(anjay, 1, 3, 0, 1));

    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 0);

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_notify_changed(anjay, 1, 3, 0, 1));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, notify_instances_changed_success) {
    LWM2M_GATEWAY_TESTS_INIT();

    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 0);

    const anjay_dm_object_def_t *mock_obj = make_mock_object(3);
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_object(anjay, iid, &mock_obj));

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_notify_instances_changed(anjay, iid, 3));

    avs_free((anjay_dm_object_def_t *) (intptr_t) mock_obj);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, notify_instances_changed_device_not_registered) {
    LWM2M_GATEWAY_TESTS_INIT();

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_notify_instances_changed(anjay, 1, 3));

    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 0);

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_notify_instances_changed(anjay, 1, 3));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, notify_instances_changed_object_not_registered) {
    LWM2M_GATEWAY_TESTS_INIT();

    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 0);

    // this function does not check whether OID is valid, it fails when anjay
    // attempts to create a notification for it.
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_notify_instances_changed(anjay, iid, 5));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, notify_instances_changed_gateway_not_installed) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER);

    AVS_UNIT_ASSERT_FAILED(
            anjay_lwm2m_gateway_notify_instances_changed(anjay, 0, 3));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(lwm2m_gateway, notify_instances_changed_invalid_oid) {
    LWM2M_GATEWAY_TESTS_INIT();

    iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_register_device(anjay, "SN01", &iid));
    AVS_UNIT_ASSERT_EQUAL(iid, 0);

    // this function does not check whether OID is valid, it fails when anjay
    // attempts to create a notification for it.
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_lwm2m_gateway_notify_instances_changed(anjay, iid, 65535));

    DM_TEST_FINISH;
}
