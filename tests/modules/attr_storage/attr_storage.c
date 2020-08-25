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

#include <avsystem/commons/avs_stream_inbuf.h>
#include <avsystem/commons/avs_unit_test.h>

#include <string.h>

#include <anjay/attr_storage.h>
#include <anjay/core.h>

#include <anjay_modules/dm/anjay_execute.h>

#include "attr_storage_test.h"
#include "src/core/dm/anjay_dm_attributes.h"
#include "tests/utils/dm.h"

//// PASSIVE PROXY HANDLERS ////////////////////////////////////////////////////

static const anjay_dm_object_def_t *const OBJ2 = &(
        const anjay_dm_object_def_t) {
    .oid = 69,
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

#define DM_ATTR_STORAGE_TEST_INIT                                          \
    DM_TEST_INIT_WITH_OBJECTS(&OBJ, &OBJ2, &FAKE_SECURITY2, &FAKE_SERVER); \
    _anjay_dm_transaction_begin(anjay);                                    \
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay))

#define DM_ATTR_STORAGE_TEST_FINISH                                      \
    do {                                                                 \
        (void) mocksocks;                                                \
        AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_transaction_finish(anjay, 0)); \
        DM_TEST_FINISH;                                                  \
    } while (0)

AVS_UNIT_TEST(attr_storage, instance_create) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 42, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_call_instance_create(anjay, &OBJ, 42, NULL));
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 0, -42);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_call_instance_create(anjay, &OBJ, 0, NULL),
                          -42);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_read) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 514, 42, ANJAY_ID_INVALID,
                                        0, ANJAY_MOCK_DM_NONE);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_read(
            anjay, &OBJ, 514, 42, ANJAY_ID_INVALID, NULL, NULL));
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 14, ANJAY_ID_INVALID,
                                        -7, ANJAY_MOCK_DM_NONE);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_call_resource_read(anjay, &OBJ, 69, 14,
                                                       ANJAY_ID_INVALID, NULL,
                                                       NULL),
                          -7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_write) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 42, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write(
            anjay, &OBJ, 514, 42, ANJAY_ID_INVALID, NULL, NULL));
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 14, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_NONE, -7);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_call_resource_write(anjay, &OBJ, 69, 14,
                                                        ANJAY_ID_INVALID, NULL,
                                                        NULL),
                          -7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_execute) {
    DM_ATTR_STORAGE_TEST_INIT;
    avs_stream_inbuf_t null_stream = AVS_STREAM_INBUF_STATIC_INITIALIZER;
    anjay_execute_ctx_t *ctx =
            _anjay_execute_ctx_create((avs_stream_t *) &null_stream);
    AVS_UNIT_ASSERT_NOT_NULL(ctx);
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 514, 42, NULL, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_call_resource_execute(anjay, &OBJ, 514, 42, ctx, NULL));
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 69, 14, NULL, -7);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_call_resource_execute(anjay, &OBJ, 69, 14,
                                                          ctx, NULL),
                          -7);
    _anjay_execute_ctx_destroy(&ctx);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

//// NOTIFICATION HANDLING /////////////////////////////////////////////////////

AVS_UNIT_TEST(attr_storage, as_notify_callback_1) {
    DM_ATTR_STORAGE_TEST_INIT;

    // prepare initial state
    AVS_LIST_APPEND(
            &get_as(anjay)->objects,
            test_object_entry(
                    42,
                    NULL,
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            0, 2, 514, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            4, 1, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            1, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 42.0,
                                            14.0, 3.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(7, NULL),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            0, 42, 44, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            7, 33, 888,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(2, NULL),
                            test_resource_entry(
                                    4,
                                    test_resource_attrs(
                                            4, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(4, NULL, NULL),
                    test_instance_entry(7, NULL, NULL),
                    test_instance_entry(
                            8,
                            test_default_attrlist(
                                    test_default_attrs(
                                            0, 0, 0, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(3, NULL),
                            NULL),
                    NULL));
    AVS_LIST_APPEND(
            &get_as(anjay)->objects,
            test_object_entry(
                    43,
                    NULL,
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            4, 2, 514, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    anjay_notify_queue_t queue = NULL;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_instance_set_unknown_change(&queue, 0));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_instance_set_unknown_change(&queue, 42));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_instance_set_unknown_change(&queue, 43));

    // server mapping:
    // /0/4/10 == 7
    // /0/7/10 == 154
    // /0/42/10 == 4
    // /0/514/10 == -4 (invalid)
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SECURITY2, 0,
            (const anjay_iid_t[]) { 4, 7, 42, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SECURITY2, 4, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SECURITY_SERVER_URI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_BOOTSTRAP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_MODE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 4, 10,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 7));
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SECURITY2, 7, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SECURITY_SERVER_URI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_BOOTSTRAP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_MODE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 7, 10,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SECURITY2, 42, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SECURITY_SERVER_URI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_BOOTSTRAP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_MODE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 42, 10,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 4));
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SECURITY2, 514, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SECURITY_SERVER_URI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_BOOTSTRAP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_MODE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 514, 10,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, -4));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 2, 3, 7, 13, 42, ANJAY_ID_INVALID });
    AVS_UNIT_ASSERT_SUCCESS(as_notify_callback(anjay, queue, get_as(anjay)));
    _anjay_notify_clear_queue(&queue);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_as(anjay)->objects), 1);
    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    42,
                    NULL,
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            7, 33, 888,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    4,
                                    test_resource_attrs(
                                            4, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));

    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_instance_set_unknown_change(&queue, 2));
    AVS_UNIT_ASSERT_SUCCESS(as_notify_callback(anjay, queue, get_as(anjay)));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_notify_clear_queue(&queue);

    // error
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_instance_set_unknown_change(&queue, 42));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, -11, (const anjay_iid_t[]) { 7, ANJAY_ID_INVALID });
    AVS_UNIT_ASSERT_FAILED(as_notify_callback(anjay, queue, get_as(anjay)));
    AVS_UNIT_ASSERT_NULL(get_as(anjay)->objects);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    _anjay_notify_clear_queue(&queue);

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, as_notify_callback_2) {
    DM_ATTR_STORAGE_TEST_INIT;

    AVS_LIST_APPEND(
            &get_as(anjay)->objects,
            test_object_entry(
                    42,
                    test_default_attrlist(
                            test_default_attrs(2, 5, 6,
                                               ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            514, 3, 4, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            3, 9, 10, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, -1.0,
                                            -2.0, -3.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            4,
                            NULL,
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    6,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            7,
                            NULL,
                            test_resource_entry(
                                    11,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    42,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            21,
                            NULL,
                            test_resource_entry(
                                    22,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    33,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            42,
                            NULL,
                            test_resource_entry(
                                    17,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    69,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    anjay_notify_queue_t queue = NULL;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_instance_set_unknown_change(&queue, 1));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_resource_change(&queue, 42, 4, 1));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_resource_change(&queue, 42, 4, 6));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_resource_change(&queue, 42, 7, 11));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_resource_change(&queue, 42, 21, 22));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_resource_change(&queue, 42, 21, 23));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_notify_queue_resource_change(&queue, 42, 42, 42));

    // server mapping:
    // /1/9/0 == 514
    // /1/10/0 == 2
    // /1/11/0 == -5 (invalid)
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 9, 10, 11, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 9, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 9, 0,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 10, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 10, 0,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 2));
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 11, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 11, 0,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, -5));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 2, 4, 7, 21, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 4, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 7, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_list_resources(anjay, &OBJ, 21, -11, NULL);
    _anjay_mock_dm_expect_list_resources(anjay, &OBJ, 42, -514, NULL);
    AVS_UNIT_ASSERT_FAILED(as_notify_callback(anjay, queue, get_as(anjay)));
    _anjay_notify_clear_queue(&queue);

    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_as(anjay)->objects), 1);
    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    42,
                    test_default_attrlist(
                            test_default_attrs(2, 5, 6,
                                               ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            514, 3, 4, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            4,
                            NULL,
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    6,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            21,
                            NULL,
                            test_resource_entry(
                                    22,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    33,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            42,
                            NULL,
                            test_resource_entry(
                                    17,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    69,
                                    test_resource_attrs(
                                            2, 1, 2, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 3.0, 4.0,
                                            5.0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    DM_ATTR_STORAGE_TEST_FINISH;
}

//// ATTRIBUTE HANDLERS ////////////////////////////////////////////////////////

AVS_UNIT_TEST(attr_storage, read_object_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_internal_oi_attrs_t attrs;
    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 4, 0, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_read_default_attrs(
            anjay, &OBJ, 4, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);

    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 42, -413, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_call_object_read_default_attrs(
                                  anjay, &OBJ, 42, &attrs, NULL),
                          -413);

    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 7, 0,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_read_default_attrs(
            anjay, &OBJ, 7, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_oi_attrs_t) {
                           .standard = {
                               .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_period = 77,
                               .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                           }
                       });
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_object_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 42,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ, 42,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 7,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ, 7,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 8,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 88,
                    .max_period = 888,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            -8888);
    AVS_UNIT_ASSERT_EQUAL(
            _anjay_dm_call_object_write_default_attrs(
                    anjay, &OBJ, 8,
                    &(const anjay_dm_internal_oi_attrs_t) {
                        .standard = {
                            .min_period = 88,
                            .max_period = 888,
                            .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                            .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                        }
                    },
                    NULL),
            -8888);

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 9,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ, 9,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 9, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ, 9, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 11, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ, 11, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));

    AVS_UNIT_ASSERT_NULL(get_as(anjay)->objects);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, object_default_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ2, 42,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ2, 7,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ2, 8, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ2, 9,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ2, 11, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_write_default_attrs(
            anjay, &OBJ2, 9, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;

    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    69,
                    test_default_attrlist(
                            test_default_attrs(7, ANJAY_ATTRIB_PERIOD_NONE, 77,
                                               ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            test_default_attrs(42, 43, ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    NULL));

    anjay_dm_internal_oi_attrs_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_read_default_attrs(
            anjay, &OBJ2, 4, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_read_default_attrs(
            anjay, &OBJ2, 42, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_oi_attrs_t) {
                           .standard = {
                               .min_period = 43,
                               .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                           },
                           _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
                       });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_object_read_default_attrs(
            anjay, &OBJ2, 7, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_oi_attrs_t) {
                           .standard = {
                               .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_period = 77,
                               .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                           },
                           _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
                       });
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_instance_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_internal_oi_attrs_t attrs;
    _anjay_mock_dm_expect_instance_read_default_attrs(
            anjay, &OBJ, 5, 4, 0, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_read_default_attrs(
            anjay, &OBJ, 5, 4, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);

    _anjay_mock_dm_expect_instance_read_default_attrs(
            anjay, &OBJ, 5, 42, -413, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_call_instance_read_default_attrs(
                                  anjay, &OBJ, 5, 42, &attrs, NULL),
                          -413);

    _anjay_mock_dm_expect_instance_read_default_attrs(
            anjay, &OBJ, 7, 4, 0,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_read_default_attrs(
            anjay, &OBJ, 7, 4, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_oi_attrs_t) {
                           .standard = {
                               .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_period = 77,
                               .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                           }
                       });
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_instance_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 4, 42,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ, 4, 42,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 4, 7,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ, 4, 7,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 8, 7,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 88,
                    .max_period = 888,
                    .min_eval_period = 8888,
                    .max_eval_period = 88888
                }
            },
            -8888);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_call_instance_write_default_attrs(
                                  anjay, &OBJ, 8, 7,
                                  &(const anjay_dm_internal_oi_attrs_t) {
                                      .standard = {
                                          .min_period = 88,
                                          .max_period = 888,
                                          .min_eval_period = 8888,
                                          .max_eval_period = 88888
                                      }
                                  },
                                  NULL),
                          -8888);

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 9, 4,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ, 9, 4,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 9, 4, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ, 9, 4, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 11, 11, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ, 11, 11, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));

    AVS_UNIT_ASSERT_NULL(get_as(anjay)->objects);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, instance_default_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ2, 42, 2, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_NULL(get_as(anjay)->objects);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ2, 3, 2,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 9,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ2, 3, 5,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 7,
                    .max_period = 15,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ2, 9, 5,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 1,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ2, 14, 5,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 10,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ2, 9, 5, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_as(anjay)->objects), 1);
    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            3,
                            test_default_attrlist(
                                    test_default_attrs(
                                            2, 4, 9, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            5, 7, 15, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            14,
                            test_default_attrlist(
                                    test_default_attrs(
                                            5, ANJAY_ATTRIB_PERIOD_NONE, 10,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    anjay_dm_internal_oi_attrs_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_read_default_attrs(
            anjay, &OBJ2, 42, 2, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_read_default_attrs(
            anjay, &OBJ2, 3, 2, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_oi_attrs_t) {
                           .standard = {
                               .min_period = 4,
                               .max_period = 9,
                               .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                           },
                           _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
                       });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_read_default_attrs(
            anjay, &OBJ2, 3, 5, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_oi_attrs_t) {
                           .standard = {
                               .min_period = 7,
                               .max_period = 15,
                               .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                           },
                           _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
                       });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_read_default_attrs(
            anjay, &OBJ2, 9, 5, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_read_default_attrs(
            anjay, &OBJ2, 14, 5, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_oi_attrs_t) {
                           .standard = {
                               .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_period = 10,
                               .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                           },
                           _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
                       });

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_resource_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_internal_r_attrs_t attrs;
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 5, 6, 4, 0,
                                              &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_read_attrs(
            anjay, &OBJ, 5, 6, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY);

    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 5, 7, 42, -413,
                                              &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_call_resource_read_attrs(anjay, &OBJ, 5, 7,
                                                             42, &attrs, NULL),
                          -413);

    _anjay_mock_dm_expect_resource_read_attrs(
            anjay, &OBJ, 7, 17, 4, 0,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 77,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 44.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = .5
                }
            });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_read_attrs(
            anjay, &OBJ, 7, 17, 4, &attrs, NULL));
    assert_res_attrs_equal(
            &attrs,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 77,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 44.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = .5
                }
            });
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_resource_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 4, 9, 42,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 43,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 13.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ, 4, 9, 42,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 43,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 13.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 4, 111, 7,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 77,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ, 4, 111, 7,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 77,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 8, 9, 7,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 0.8,
                    .less_than = 8.8,
                    .step = 88.8
                }
            },
            -8888);
    AVS_UNIT_ASSERT_EQUAL(
            _anjay_dm_call_resource_write_attrs(
                    anjay, &OBJ, 8, 9, 7,
                    &(const anjay_dm_internal_r_attrs_t) {
                        .standard = {
                            .common = {
                                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                                .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                            },
                            .greater_than = 0.8,
                            .less_than = 8.8,
                            .step = 88.8
                        }
                    },
                    NULL),
            -8888);

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 4,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 99.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE,
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 4,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 99.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE,
                }
            },
            NULL));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, NULL));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 11, 11, 11, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ, 11, 11, 11, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, NULL));

    AVS_UNIT_ASSERT_NULL(get_as(anjay)->objects);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_resource_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;

    AVS_LIST_APPEND(
            &get_as(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            3, NULL,
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            42, 1, 2, 6, 7, 3.0, 4.0, 5.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    anjay_dm_internal_r_attrs_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_read_attrs(
            anjay, &OBJ2, 3, 1, 42, &attrs, NULL));
    assert_res_attrs_equal(&attrs,
                           &(const anjay_dm_internal_r_attrs_t) {
                               .standard = {
                                   .common = {
                                       .min_period = 1,
                                       .max_period = 2,
                                       .min_eval_period = 6,
                                       .max_eval_period = 7
                                   },
                                   .greater_than = 3.0,
                                   .less_than = 4.0,
                                   .step = 5.0
                               },
                               _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
                           });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_read_attrs(
            anjay, &OBJ2, 3, 1, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_read_attrs(
            anjay, &OBJ2, 3, 2, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_read_attrs(
            anjay, &OBJ2, 2, 2, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_resource_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 5, 3, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, NULL));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_NULL(get_as(anjay)->objects);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 1,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 1,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 34.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_as(anjay)->objects), 1);
    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            NULL,
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            1, 1, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 34.0,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 5, 3,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 4,
                        .max_period = 5,
                        .min_eval_period = 99,
                        .max_eval_period = 100
                    },
                    .greater_than = 6.0,
                    .less_than = 7.0,
                    .step = 8.0
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 5,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 9,
                        .max_period = 10,
                        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 11.0,
                    .less_than = 22.0,
                    .step = 33.0
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_as(anjay)->objects), 1);
    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            NULL,
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            1, 1, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 34.0,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_resource_attrs(
                                            5, 9, 10, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE, 11.0,
                                            22.0, 33.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    5,
                                    test_resource_attrs(
                                            3, 4, 5, 99, 100, 6.0, 7.0, 8.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ2, 2, 4,
            &(const anjay_dm_internal_oi_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 5, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 1, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_as(anjay)->objects), 1);
    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            4, 4, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    5,
                                    test_resource_attrs(
                                            3, 4, 5, 99, 100, 6.0, 7.0, 8.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 5, 3, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_as(anjay)->objects), 1);
    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            4, 4, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 5,
            &(const anjay_dm_internal_r_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 9,
                        .max_period = 10,
                        .min_eval_period = 11,
                        .max_eval_period = 12
                    },
                    .greater_than = 11.0,
                    .less_than = 22.0,
                    .step = 33.0
                },
                _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
            },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_instance_write_default_attrs(
            anjay, &OBJ2, 2, 4, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_as(anjay)->objects), 1);
    assert_object_equal(
            get_as(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            NULL,
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            5, 9, 10, 11, 12, 11.0, 22.0, 33.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_call_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 5, &ANJAY_DM_INTERNAL_R_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_as(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_NULL(get_as(anjay)->objects);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

//// SSID HANDLING /////////////////////////////////////////////////////////////

AVS_UNIT_TEST(set_attribs, fail_on_null_attribs) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ_NOATTRS, &FAKE_SECURITY2);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay));

    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_object_attrs(
            anjay, 1, OBJ_NOATTRS->oid, NULL));
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_instance_attrs(
            anjay, 1, OBJ_NOATTRS->oid, 30, NULL));
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
            anjay, 1, OBJ_NOATTRS->oid, 30, 50, NULL));
    DM_TEST_FINISH;
}

static const anjay_dm_oi_attributes_t *FAKE_DM_ATTRS =
        (anjay_dm_oi_attributes_t *) -1;
static const anjay_dm_r_attributes_t *FAKE_DM_RES_ATTRS =
        (anjay_dm_r_attributes_t *) -1;

AVS_UNIT_TEST(set_attribs, fail_on_invalid_ssid) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ_NOATTRS, &FAKE_SERVER);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay));

    const anjay_ssid_t SSIDS_TO_TEST[] = { ANJAY_SSID_ANY, ANJAY_SSID_BOOTSTRAP,
                                           341 };
    // Assumming no Server Instances
    for (int i = 0; i < (int) AVS_ARRAY_SIZE(SSIDS_TO_TEST); ++i) {
        // object
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_ANY
                && SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_list_instances(anjay, &FAKE_SERVER, 0,
                                                 (const anjay_iid_t[]) {
                                                         ANJAY_ID_INVALID });
        }
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_object_attrs(
                anjay, SSIDS_TO_TEST[i], OBJ_NOATTRS->oid, FAKE_DM_ATTRS));

        // instance
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_ANY
                && SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_list_instances(anjay, &FAKE_SERVER, 0,
                                                 (const anjay_iid_t[]) {
                                                         ANJAY_ID_INVALID });
        }
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_instance_attrs(
                anjay, SSIDS_TO_TEST[i], OBJ_NOATTRS->oid, 0, FAKE_DM_ATTRS));

        // resource
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_ANY
                && SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_list_instances(anjay, &FAKE_SERVER, 0,
                                                 (const anjay_iid_t[]) {
                                                         ANJAY_ID_INVALID });
        }
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
                anjay, SSIDS_TO_TEST[i], OBJ_NOATTRS->oid, 0, 0,
                FAKE_DM_RES_ATTRS));
    }

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(set_attribs, fail_on_invalid_object) {
    DM_TEST_INIT_WITH_SSIDS(1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay));

    // query SSID
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
            anjay_attr_storage_set_object_attrs(anjay, 1, 5, FAKE_DM_ATTRS));

    // query SSID
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
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_instance_attrs(
            anjay, 1, 5, 1, FAKE_DM_ATTRS));

    // query SSID
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
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
            anjay, 1, 5, 1, 0, FAKE_DM_RES_ATTRS));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(set_attribs, fail_on_invalid_iid) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ_NOATTRS, &FAKE_SERVER);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay));

    // attempt to query SSID
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });

    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ_NOATTRS, 0, (const anjay_iid_t[]) { ANJAY_ID_INVALID });
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_instance_attrs(
            anjay, 1, OBJ_NOATTRS->oid, ANJAY_ID_INVALID, FAKE_DM_ATTRS));

    // attempt to query SSID
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });

    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ_NOATTRS, 0, (const anjay_iid_t[]) { ANJAY_ID_INVALID });
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
            anjay, 1, OBJ_NOATTRS->oid, ANJAY_ID_INVALID, 1,
            FAKE_DM_RES_ATTRS));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(set_attribs, fail_on_invalid_rid) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ_NOATTRS, &FAKE_SERVER);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay));

    // attempt to query SSID
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });

    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) { { ANJAY_DM_RID_SERVER_SSID,
                                                    ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ_NOATTRS, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });

    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ_NOATTRS, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
            anjay, 1, OBJ_NOATTRS->oid, 1, 1, FAKE_DM_RES_ATTRS));

    DM_TEST_FINISH;
}
