/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <anjay/access_control.h>
#include <anjay/core.h>

#include <anjay_modules/dm/anjay_execute.h>

#include "src/core/anjay_core.h"
#include "src/core/servers/anjay_servers_internal.h"
#include "src/modules/access_control/anjay_mod_access_control.h"
#include "tests/utils/dm.h"

#define TEST_OID 0x100
static const anjay_dm_object_def_t *const TEST = &(
        const anjay_dm_object_def_t) {
    .oid = TEST_OID,
    .handlers = {
        .list_instances = _anjay_mock_dm_list_instances,
        .instance_create = _anjay_mock_dm_instance_create,
        .instance_remove = _anjay_mock_dm_instance_remove,
        .list_resources = _anjay_mock_dm_list_resources,
        .resource_read = _anjay_mock_dm_resource_read,
        .resource_write = _anjay_mock_dm_resource_write,
        .resource_execute = _anjay_mock_dm_resource_execute,
        .list_resource_instances = _anjay_mock_dm_list_resource_instances
    }
};

#define ACCESS_CONTROL_TEST_INIT                                            \
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER, &TEST);         \
                                                                            \
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_install(anjay));           \
                                                                            \
    /* prevent sending Update, as that will fail in the test environment */ \
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);                                \
    avs_sched_del(&anjay_unlocked->servers->next_action_handle);            \
    ANJAY_MUTEX_UNLOCK(anjay);                                              \
                                                                            \
    anjay_sched_run(anjay)

AVS_UNIT_TEST(access_control, set_acl) {
    ACCESS_CONTROL_TEST_INIT;

    const anjay_iid_t iid = 1;
    const anjay_ssid_t ssid = 1;

    {
        ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
        anjay_notify_queue_t queue = NULL;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_notify_queue_instance_created(&queue, TEST->oid, iid));

        // transaction validation
        _anjay_mock_dm_expect_list_instances(
                anjay, &TEST, 0, (anjay_iid_t[]){ iid, ANJAY_ID_INVALID });
        _anjay_mock_dm_expect_list_instances(
                anjay, &FAKE_SERVER, 0,
                (const anjay_iid_t[]) { 0, ANJAY_ID_INVALID });
        _anjay_mock_dm_expect_list_resources(
                anjay, &FAKE_SERVER, 0, 0,
                (const anjay_mock_dm_res_entry_t[]) {
                        { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                          ANJAY_DM_RES_PRESENT },
                        ANJAY_MOCK_DM_RES_END });
        _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 0,
                                            ANJAY_DM_RID_SERVER_SSID,
                                            ANJAY_ID_INVALID, 0,
                                            ANJAY_MOCK_DM_INT(0, ssid));
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_notify_flush(anjay_unlocked, ssid, &queue));
        ANJAY_MUTEX_UNLOCK(anjay);
    }

    // NULL AC object ptr
    AVS_UNIT_ASSERT_FAILED(anjay_access_control_set_acl(
            NULL, TEST->oid, iid, ssid, ANJAY_ACCESS_MASK_NONE));

    // unknown Object ID
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_acl(anjay, (anjay_oid_t) (TEST->oid + 1),
                                         iid, ssid, ANJAY_ACCESS_MASK_NONE));

    // unknown Object instance ID
    anjay_iid_t iids[iid + 2];
    for (anjay_iid_t i = 0; i <= iid; ++i) {
        iids[i] = i;
    }
    iids[iid + 1] = ANJAY_ID_INVALID;
    _anjay_mock_dm_expect_list_instances(anjay, &TEST, 0, iids);
    AVS_UNIT_ASSERT_FAILED(anjay_access_control_set_acl(
            anjay, TEST->oid, (anjay_iid_t) (iid + 1), ssid,
            ANJAY_ACCESS_MASK_NONE));

    // Create flag in access mask
    AVS_UNIT_ASSERT_FAILED(anjay_access_control_set_acl(
            anjay, TEST->oid, iid, ssid, ANJAY_ACCESS_MASK_CREATE));
    AVS_UNIT_ASSERT_FAILED(anjay_access_control_set_acl(
            anjay, TEST->oid, iid, ssid, ANJAY_ACCESS_MASK_FULL));

    {
        // valid call
        anjay_access_mask_t mask =
                ANJAY_ACCESS_MASK_READ | ANJAY_ACCESS_MASK_WRITE
                | ANJAY_ACCESS_MASK_EXECUTE | ANJAY_ACCESS_MASK_DELETE;
        AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_set_acl(anjay, TEST->oid,
                                                             iid, ssid, mask));

        ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
        access_control_t *ac = _anjay_access_control_get(anjay_unlocked);
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 1);

        AVS_LIST(access_control_instance_t) inst = ac->current.instances;
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(inst->acl), 1);

        AVS_UNIT_ASSERT_EQUAL(inst->acl->ssid, ssid);
        AVS_UNIT_ASSERT_EQUAL(inst->acl->mask, mask);
        ANJAY_MUTEX_UNLOCK(anjay);
    }

    {
        // overwrite existing entry
        anjay_access_mask_t mask = ANJAY_ACCESS_MASK_READ;
        AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_set_acl(anjay, TEST->oid,
                                                             iid, ssid, mask));

        ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
        access_control_t *ac = _anjay_access_control_get(anjay_unlocked);
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 1);

        AVS_LIST(access_control_instance_t) inst = ac->current.instances;
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(inst->acl), 1);

        // ensure mask was overwritten
        AVS_UNIT_ASSERT_EQUAL(inst->acl->ssid, ssid);
        AVS_UNIT_ASSERT_EQUAL(inst->acl->mask, mask);
        ANJAY_MUTEX_UNLOCK(anjay);
    }

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(access_control, set_owner) {
    ACCESS_CONTROL_TEST_INIT;

    // SSID == 0 is invalid
    AVS_UNIT_ASSERT_FAILED(anjay_access_control_set_owner(
            anjay, TEST->oid, 1, ANJAY_SSID_ANY, NULL));

    // Basic happy path
    _anjay_mock_dm_expect_list_instances(
            anjay, &TEST, 0, (anjay_iid_t[]){ 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 0, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 0, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 0,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_access_control_set_owner(anjay, TEST->oid, 1, 1, NULL));
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    access_control_t *ac = _anjay_access_control_get(anjay_unlocked);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 1);
    AVS_LIST(access_control_instance_t) inst = ac->current.instances;
    AVS_UNIT_ASSERT_EQUAL(inst->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(inst->owner, 1);
    ANJAY_MUTEX_UNLOCK(anjay);

    // Conflicting Access Control Object Instance ID
    anjay_iid_t inout_acl_iid = 1;
    AVS_UNIT_ASSERT_FAILED(anjay_access_control_set_owner(anjay, TEST->oid, 1,
                                                          2, &inout_acl_iid));
    AVS_UNIT_ASSERT_EQUAL(inout_acl_iid, 0);

    // Validation failure: inexistent target
    _anjay_mock_dm_expect_list_instances(
            anjay, &TEST, 0, (anjay_iid_t[]){ 1, ANJAY_ID_INVALID });
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_owner(anjay, TEST->oid, 2, 1, NULL));

    // Happy path with reading of Access Control Object Instance ID
    _anjay_mock_dm_expect_list_instances(
            anjay, &TEST, 0, (anjay_iid_t[]){ 1, 2, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 0, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 0, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 0,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    inout_acl_iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_set_owner(anjay, TEST->oid, 2,
                                                           1, &inout_acl_iid));
    AVS_UNIT_ASSERT_EQUAL(inout_acl_iid, 1);
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    access_control_t *ac = _anjay_access_control_get(anjay_unlocked);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 2);
    ANJAY_MUTEX_UNLOCK(anjay);

    // SSID validation error (existing target)
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 0, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 0, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 0,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_owner(anjay, TEST->oid, 2, 2, NULL));

    // SSID validation error (new target)
    _anjay_mock_dm_expect_list_instances(
            anjay, &TEST, 0, (anjay_iid_t[]){ 1, 2, 3, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 0, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 0, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 0,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_owner(anjay, TEST->oid, 3, 2, NULL));

    // No-op
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_set_owner(anjay, TEST->oid, 2,
                                                           1, &inout_acl_iid));

    // Changing owner to the Bootstrap Server
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_set_owner(
            anjay, TEST->oid, 2, ANJAY_SSID_BOOTSTRAP, &inout_acl_iid));

    // Happy path with setting of Access Control Object Instance ID
    _anjay_mock_dm_expect_list_instances(
            anjay, &TEST, 0, (anjay_iid_t[]){ 1, 2, 21, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 0, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 0, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 0,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    inout_acl_iid = 37;
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_set_owner(anjay, TEST->oid, 21,
                                                           1, &inout_acl_iid));
    AVS_UNIT_ASSERT_EQUAL(inout_acl_iid, 37);
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    access_control_t *ac = _anjay_access_control_get(anjay_unlocked);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 3);
    AVS_UNIT_ASSERT_EQUAL(ac->current.instances->iid, 0);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_NEXT(ac->current.instances)->iid, 1);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_NEXT(AVS_LIST_NEXT(ac->current.instances))->iid, 37);
    ANJAY_MUTEX_UNLOCK(anjay);

    // Attempting to reuse existing Access Control Object Instance ID
    _anjay_mock_dm_expect_list_instances(
            anjay, &TEST, 0, (anjay_iid_t[]){ 1, 2, 21, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 0, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 0, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 0,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    inout_acl_iid = 37;
    AVS_UNIT_ASSERT_FAILED(anjay_access_control_set_owner(anjay, TEST->oid, 42,
                                                          1, &inout_acl_iid));
    AVS_UNIT_ASSERT_EQUAL(inout_acl_iid, 37);

    DM_TEST_FINISH;
}
