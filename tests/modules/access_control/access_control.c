/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

AVS_UNIT_TEST(access_control, set_acl) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY, &FAKE_SERVER, &TEST);
    const anjay_iid_t iid = 1;
    const anjay_ssid_t ssid = 1;

    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_install(anjay));

    // prevent sending Update, as that will fail in the test environment
    avs_sched_del(&anjay->servers->servers->next_action_handle);

    anjay_sched_run(anjay);

    {
        anjay_notify_queue_t queue = NULL;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_notify_queue_instance_created(&queue, TEST->oid, iid));
        anjay->current_connection.server = anjay->servers->servers;
        anjay->current_connection.conn_type = ANJAY_CONNECTION_PRIMARY;

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
        AVS_UNIT_ASSERT_SUCCESS(_anjay_notify_flush(anjay, &queue));
        memset(&anjay->current_connection, 0,
               sizeof(anjay->current_connection));
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

        access_control_t *ac = _anjay_access_control_get(anjay);
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 1);

        AVS_LIST(access_control_instance_t) inst = ac->current.instances;
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(inst->acl), 1);

        AVS_UNIT_ASSERT_EQUAL(inst->acl->ssid, ssid);
        AVS_UNIT_ASSERT_EQUAL(inst->acl->mask, mask);
    }

    {
        // overwrite existing entry
        anjay_access_mask_t mask = ANJAY_ACCESS_MASK_READ;
        AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_set_acl(anjay, TEST->oid,
                                                             iid, ssid, mask));

        access_control_t *ac = _anjay_access_control_get(anjay);
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac->current.instances), 1);

        AVS_LIST(access_control_instance_t) inst = ac->current.instances;
        AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(inst->acl), 1);

        // ensure mask was overwritten
        AVS_UNIT_ASSERT_EQUAL(inst->acl->ssid, ssid);
        AVS_UNIT_ASSERT_EQUAL(inst->acl->mask, mask);
    }

    DM_TEST_FINISH;
}
