/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>

#include <avsystem/commons/unit/test.h>

#include <anjay/access_control.h>
#include <anjay/anjay.h>

#include <anjay_modules/dm/execute.h>

#include <anjay_test/dm.h>

#include "../access_control.h"

#define TEST_OID 0x100
static const anjay_dm_object_def_t *const TEST = &(const anjay_dm_object_def_t) {
    .oid = TEST_OID,
    .rid_bound = 7,
    .instance_it = _anjay_mock_dm_instance_it,
    .instance_present = _anjay_mock_dm_instance_present,
    .instance_create = _anjay_mock_dm_instance_create,
    .instance_remove = _anjay_mock_dm_instance_remove,
    .resource_present = _anjay_mock_dm_resource_present,
    .resource_read = _anjay_mock_dm_resource_read,
    .resource_write = _anjay_mock_dm_resource_write,
    .resource_execute = _anjay_mock_dm_resource_execute,
    .resource_dim = _anjay_mock_dm_resource_dim
};

AVS_UNIT_TEST(access_control, set_acl) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &TEST);
    const anjay_iid_t iid = 1;
    const anjay_ssid_t ssid = 1;

    const anjay_dm_object_def_t *const *aco =
            anjay_access_control_object_new(anjay);
    AVS_UNIT_ASSERT_NOT_NULL(aco);

    _anjay_mock_dm_expect_instance_it(anjay, &TEST, 0, 0, iid);
    _anjay_mock_dm_expect_instance_it(anjay, &TEST, 1, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(anjay, aco));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // NULL AC object ptr
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_acl(NULL, TEST->oid, iid, ssid,
                                         ANJAY_ACCESS_MASK_NONE));

    // unknown Object ID
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_acl(aco, (anjay_oid_t)(TEST->oid + 1), iid,
                                         ssid, ANJAY_ACCESS_MASK_NONE));

    // uknown Object instance ID
    _anjay_mock_dm_expect_instance_present(anjay, &TEST, (anjay_iid_t)(iid + 1),
                                           0);
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_acl(aco, TEST->oid, (anjay_iid_t)(iid + 1),
                                         ssid, ANJAY_ACCESS_MASK_NONE));

    // Create flag in access mask
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_acl(aco, TEST->oid, iid, ssid,
                                         ANJAY_ACCESS_MASK_CREATE));
    AVS_UNIT_ASSERT_FAILED(
            anjay_access_control_set_acl(aco, TEST->oid, iid, ssid,
                                         ANJAY_ACCESS_MASK_FULL));

    {
        // valid call
        anjay_access_mask_t mask = ANJAY_ACCESS_MASK_READ
                                 | ANJAY_ACCESS_MASK_WRITE
                                 | ANJAY_ACCESS_MASK_EXECUTE
                                 | ANJAY_ACCESS_MASK_DELETE;
        AVS_UNIT_ASSERT_SUCCESS(
                anjay_access_control_set_acl(aco, TEST->oid, iid, ssid, mask));

        // the second instance refers to the whole object
        access_control_t *ac = _anjay_access_control_get(aco);
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 2);

        AVS_LIST(access_control_instance_t) inst = ac->current.instances;
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(inst->acl), 1);

        AVS_UNIT_ASSERT_EQUAL(inst->acl->ssid, ssid);
        AVS_UNIT_ASSERT_EQUAL(inst->acl->mask, mask);
    }

    {
        // overwrite existing entry
        anjay_access_mask_t mask = ANJAY_ACCESS_MASK_READ;
        AVS_UNIT_ASSERT_SUCCESS(
                anjay_access_control_set_acl(aco, TEST->oid, iid, ssid, mask));

        // make sure no more stuff got allocated
        access_control_t *ac = _anjay_access_control_get(aco);
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 2);

        AVS_LIST(access_control_instance_t) inst = ac->current.instances;
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(inst->acl), 1);

        // ensure mask was overwritten
        AVS_UNIT_ASSERT_EQUAL(inst->acl->ssid, ssid);
        AVS_UNIT_ASSERT_EQUAL(inst->acl->mask, mask);
    }

    anjay_access_control_object_delete(aco);
    DM_TEST_FINISH;
}
