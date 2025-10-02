/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_unit_test.h>

#include <anjay/factory_provisioning.h>

#include "tests/utils/dm.h"

// NOTE: Success case is tested by
// tests/integration/suites/defaultfactory_provisioning.py

AVS_UNIT_TEST(factory_provisioning, fail_rollback) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ_WITH_TRANSACTION, &FAKE_SECURITY,
                              &FAKE_SERVER);
    avs_stream_t *stream = avs_stream_membuf_create();
    AVS_UNIT_ASSERT_NOT_NULL(stream);
    static const char PROVISIONING_DATA[] = "\x82" // array(2)
                                            "\xa2" // map(2)
                                            "\x00\x69"
                                            "/69/420/2" // name: "/69/420/2"
                                            "\x02\x01"  // value: 1
                                            "\xa2"      // map(2)
                                            "\x00\x69"
                                            "/69/420/3" // name: "/69/420/3"
                                            "\x02\x07"; // value: 7
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, PROVISIONING_DATA,
                                             sizeof(PROVISIONING_DATA) - 1));
    avs_unit_mocksock_expect_shutdown(mocksocks[0]);
    // Implicit DELETE /
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0, (const anjay_iid_t[]) { ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_instances(anjay, &OBJ_WITH_TRANSACTION, 0,
                                         (const anjay_iid_t[]) {
                                                 ANJAY_ID_INVALID });
    // actual write
    _anjay_mock_dm_expect_list_instances(anjay, &OBJ_WITH_TRANSACTION, 0,
                                         (const anjay_iid_t[]) {
                                                 ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_transaction_begin(anjay, &OBJ_WITH_TRANSACTION, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ_WITH_TRANSACTION, 420, 0);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ_WITH_TRANSACTION, 420, 2,
                                         ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 1), 0);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ_WITH_TRANSACTION, 0,
            (const anjay_iid_t[]) { 420, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ_WITH_TRANSACTION, 420, 3,
                                         ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 7), 0);
    // fail transaction validation
    _anjay_mock_dm_expect_transaction_validate(anjay, &OBJ_WITH_TRANSACTION,
                                               -1);
    _anjay_mock_dm_expect_transaction_rollback(anjay, &OBJ_WITH_TRANSACTION, 0);
    AVS_UNIT_ASSERT_FAILED(anjay_factory_provision(anjay, stream));
    avs_stream_cleanup(&stream);
    DM_TEST_FINISH;
}
