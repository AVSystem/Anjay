/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <avsystem/commons/unit/test.h>

#include <anjay/attr_storage.h>
#include <anjay/core.h>

#include <anjay_modules/dm/execute.h>

#include <anjay_test/dm.h>

#include "attr_storage_test.h"

#include <string.h>

//// PASSIVE PROXY HANDLERS ////////////////////////////////////////////////////

static const anjay_dm_object_def_t *const OBJ2 =
        &(const anjay_dm_object_def_t) {
            .oid = 69,
            .supported_rids = ANJAY_DM_SUPPORTED_RIDS(0, 1, 2, 3, 4, 5, 6),
            .handlers = {
                .instance_it = _anjay_mock_dm_instance_it,
                .instance_present = _anjay_mock_dm_instance_present,
                .instance_create = _anjay_mock_dm_instance_create,
                .instance_remove = _anjay_mock_dm_instance_remove,
                .resource_present = _anjay_mock_dm_resource_present,
                .resource_read = _anjay_mock_dm_resource_read,
                .resource_write = _anjay_mock_dm_resource_write,
                .resource_execute = _anjay_mock_dm_resource_execute,
                .resource_dim = _anjay_mock_dm_resource_dim
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
    anjay_iid_t iid = 42;
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 42, 1, 0, 42);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_create(anjay, &OBJ, &iid, 1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 42);
    iid = 0;
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 0, 1, -42, 69);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_create(anjay, &OBJ, &iid, 1, NULL),
                          -42);
    AVS_UNIT_ASSERT_EQUAL(iid, 69);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_read) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 514, 42, 0,
                                        ANJAY_MOCK_DM_NONE);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_resource_read(anjay, &OBJ, 514, 42, NULL, NULL));
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 14, -7,
                                        ANJAY_MOCK_DM_NONE);
    AVS_UNIT_ASSERT_EQUAL(
            _anjay_dm_resource_read(anjay, &OBJ, 69, 14, NULL, NULL), -7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_write) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 42,
                                         ANJAY_MOCK_DM_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_resource_write(anjay, &OBJ, 514, 42, NULL, NULL));
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 14,
                                         ANJAY_MOCK_DM_NONE, -7);
    AVS_UNIT_ASSERT_EQUAL(
            _anjay_dm_resource_write(anjay, &OBJ, 69, 14, NULL, NULL), -7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_execute) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_execute_ctx_t *ctx = _anjay_execute_ctx_create(NULL);
    AVS_UNIT_ASSERT_NOT_NULL(ctx);
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 514, 42,
                                           ANJAY_MOCK_DM_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_resource_execute(anjay, &OBJ, 514, 42, ctx, NULL));
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 69, 14,
                                           ANJAY_MOCK_DM_NONE, -7);
    AVS_UNIT_ASSERT_EQUAL(
            _anjay_dm_resource_execute(anjay, &OBJ, 69, 14, ctx, NULL), -7);
    _anjay_execute_ctx_destroy(&ctx);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_dim) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 514, 42, 17);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_dim(anjay, &OBJ, 514, 42, NULL),
                          17);
    _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 69, 14, -7);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_dim(anjay, &OBJ, 69, 14, NULL),
                          -7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

//// ACTIVE PROXY HANDLERS /////////////////////////////////////////////////////

AVS_UNIT_TEST(attr_storage, instance_it) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_iid_t iid;
    void *cookie = NULL;

    // prepare initial state
    AVS_LIST_APPEND(
            &get_fas(anjay)->objects,
            test_object_entry(
                    42,
                    NULL,
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            0, 2, 514,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            4, 1, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            1,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            42.0,
                                            14.0,
                                            3.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(7, NULL),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            0, 42, 44,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            7, 33, 888,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(2, NULL),
                            test_resource_entry(
                                    4,
                                    test_resource_attrs(
                                            4, 1, 2, 3.0, 4.0, 5.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(4, NULL, NULL),
                    test_instance_entry(7, NULL, NULL),
                    test_instance_entry(
                            8,
                            test_default_attrlist(
                                    test_default_attrs(
                                            0, 0, 0, ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(3, NULL),
                            NULL),
                    NULL));

    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 7);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 7);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 13);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 13);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 4, 0, 42);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 42);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 5, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    42,
                    NULL,
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            0, 42, 44,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            7, 33, 888,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(2, NULL),
                            test_resource_entry(
                                    4,
                                    test_resource_attrs(
                                            4, 1, 2, 3.0, 4.0, 5.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(7, NULL, NULL),
                    NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));

    // error
    get_fas(anjay)->modified_since_persist = false;
    cookie = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, -11, 7);
    AVS_UNIT_ASSERT_EQUAL(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie, NULL), -11);
    AVS_UNIT_ASSERT_EQUAL(iid, 7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, instance_present) {
    DM_ATTR_STORAGE_TEST_INIT;

    // prepare initial state
    AVS_LIST_APPEND(
            &get_fas(anjay)->objects,
            test_object_entry(
                    42, NULL,
                    test_instance_entry(4, NULL, test_resource_entry(33, NULL),
                                        test_resource_entry(69, NULL), NULL),
                    test_instance_entry(7, NULL, test_resource_entry(11, NULL),
                                        NULL),
                    test_instance_entry(21, NULL, test_resource_entry(22, NULL),
                                        NULL),
                    test_instance_entry(42, NULL, test_resource_entry(17, NULL),
                                        NULL),
                    NULL));

    // tests
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 42, 1);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_present(anjay, &OBJ, 42, NULL), 1);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE((*find_object(get_fas(anjay), 42))->instances), 4);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 21, -1);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_present(anjay, &OBJ, 21, NULL),
                          -1);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE((*find_object(get_fas(anjay), 42))->instances), 4);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 4, 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_present(anjay, &OBJ, 4, NULL), 0);

    // verification
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    42, NULL,
                    test_instance_entry(7, NULL, test_resource_entry(11, NULL),
                                        NULL),
                    test_instance_entry(21, NULL, test_resource_entry(22, NULL),
                                        NULL),
                    test_instance_entry(42, NULL, test_resource_entry(17, NULL),
                                        NULL),
                    NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, instance_remove) {
    DM_ATTR_STORAGE_TEST_INIT;

    // prepare initial state
    AVS_LIST_APPEND(
            &get_fas(anjay)->objects,
            test_object_entry(
                    42, NULL,
                    test_instance_entry(4, NULL, test_resource_entry(33, NULL),
                                        test_resource_entry(69, NULL), NULL),
                    test_instance_entry(7, NULL, test_resource_entry(11, NULL),
                                        NULL),
                    test_instance_entry(42, NULL, test_resource_entry(17, NULL),
                                        NULL),
                    NULL));

    // tests
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 42, 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_remove(anjay, &OBJ, 42, NULL), 0);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE((*find_object(get_fas(anjay), 42))->instances), 2);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 2, 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_remove(anjay, &OBJ, 2, NULL), 0);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE((*find_object(get_fas(anjay), 42))->instances), 2);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 7, -44);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_remove(anjay, &OBJ, 7, NULL), -44);

    // verification
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    42, NULL,
                    test_instance_entry(4, NULL, test_resource_entry(33, NULL),
                                        test_resource_entry(69, NULL), NULL),
                    test_instance_entry(7, NULL, test_resource_entry(11, NULL),
                                        NULL),
                    NULL));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_present) {
    DM_ATTR_STORAGE_TEST_INIT;

    // prepare initial state
    AVS_LIST_APPEND(
            &get_fas(anjay)->objects,
            test_object_entry(
                    42, NULL,
                    test_instance_entry(4, NULL, test_resource_entry(11, NULL),
                                        test_resource_entry(33, NULL),
                                        test_resource_entry(69, NULL), NULL),
                    test_instance_entry(7, NULL, test_resource_entry(11, NULL),
                                        test_resource_entry(42, NULL), NULL),
                    test_instance_entry(21, NULL, test_resource_entry(22, NULL),
                                        test_resource_entry(33, NULL), NULL),
                    test_instance_entry(42, NULL, test_resource_entry(17, NULL),
                                        test_resource_entry(69, NULL), NULL),
                    NULL));

    // tests
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 4, 42, 1);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_present(anjay, &OBJ, 4, 42, NULL),
                          1);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 42, 17, -1);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_present(anjay, &OBJ, 42, 17, NULL),
                          -1);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 4, 33, 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_present(anjay, &OBJ, 4, 33, NULL),
                          0);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 42, 69, 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_present(anjay, &OBJ, 42, 69, NULL),
                          0);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE((*find_object(get_fas(anjay), 42))->instances), 4);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 7, 11, 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_present(anjay, &OBJ, 7, 11, NULL),
                          0);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 7, 42, 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_present(anjay, &OBJ, 7, 42, NULL),
                          0);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));

    // verification
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    42, NULL,
                    test_instance_entry(4, NULL, test_resource_entry(11, NULL),
                                        test_resource_entry(69, NULL), NULL),
                    test_instance_entry(21, NULL, test_resource_entry(22, NULL),
                                        test_resource_entry(33, NULL), NULL),
                    test_instance_entry(42, NULL, test_resource_entry(17, NULL),
                                        NULL),
                    NULL));
    DM_ATTR_STORAGE_TEST_FINISH;
}

//// ATTRIBUTE HANDLERS ////////////////////////////////////////////////////////

AVS_UNIT_TEST(attr_storage, read_object_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_internal_attrs_t attrs;
    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 4, 0, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_object_read_default_attrs(anjay, &OBJ, 4, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);

    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 42, -413, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_object_read_default_attrs(anjay, &OBJ, 42,
                                                              &attrs, NULL),
                          -413);

    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 7, 0,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77
                }
            });
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_object_read_default_attrs(anjay, &OBJ, 7, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_attrs_t) {
                           .standard = {
                               .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_period = 77
                           }
                       });
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_object_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 42,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ, 42,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 7,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ, 7,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77
                }
            },
            NULL));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 8,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 88,
                    .max_period = 888
                }
            },
            -8888);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_object_write_default_attrs(
                                  anjay, &OBJ, 8,
                                  &(const anjay_dm_internal_attrs_t) {
                                      .standard = {
                                          .min_period = 88,
                                          .max_period = 888
                                      }
                                  },
                                  NULL),
                          -8888);

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 9,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ, 9,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99
                }
            },
            NULL));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 9, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ, 9, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 11, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ, 11, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));

    AVS_UNIT_ASSERT_NULL(get_fas(anjay)->objects);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, object_default_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ2, 42,
            &(const anjay_dm_internal_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .min_period = 43,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ2, 7,
            &(const anjay_dm_internal_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 77
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ2, 8, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ2, 9,
            &(const anjay_dm_internal_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .min_period = 4,
                        .max_period = 99
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ2, 11, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, &OBJ2, 9, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    69,
                    test_default_attrlist(
                            test_default_attrs(7, ANJAY_ATTRIB_PERIOD_NONE, 77,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            test_default_attrs(42, 43, ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    NULL));

    anjay_dm_internal_attrs_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_object_read_default_attrs(anjay, &OBJ2, 4, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_read_default_attrs(
            anjay, &OBJ2, 42, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_attrs_t) {
                               _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                   .min_period = 43,
                                   .max_period = ANJAY_ATTRIB_PERIOD_NONE
                               } });
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_object_read_default_attrs(anjay, &OBJ2, 7, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_attrs_t) {
                               _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                   .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                   .max_period = 77
                               } });
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_instance_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_internal_attrs_t attrs;
    _anjay_mock_dm_expect_instance_read_default_attrs(
            anjay, &OBJ, 5, 4, 0, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_read_default_attrs(
            anjay, &OBJ, 5, 4, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);

    _anjay_mock_dm_expect_instance_read_default_attrs(
            anjay, &OBJ, 5, 42, -413, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_read_default_attrs(
                                  anjay, &OBJ, 5, 42, &attrs, NULL),
                          -413);

    _anjay_mock_dm_expect_instance_read_default_attrs(
            anjay, &OBJ, 7, 4, 0,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77
                }
            });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_read_default_attrs(
            anjay, &OBJ, 7, 4, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_attrs_t) {
                           .standard = {
                               .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                               .max_period = 77
                           }
                       });
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_instance_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 4, 42,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ, 4, 42,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 43,
                    .max_period = ANJAY_ATTRIB_PERIOD_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 4, 7,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ, 4, 7,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                    .max_period = 77
                }
            },
            NULL));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 8, 7,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 88,
                    .max_period = 888
                }
            },
            -8888);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_instance_write_default_attrs(
                                  anjay, &OBJ, 8, 7,
                                  &(const anjay_dm_internal_attrs_t) {
                                      .standard = {
                                          .min_period = 88,
                                          .max_period = 888
                                      }
                                  },
                                  NULL),
                          -8888);

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 9, 4,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ, 9, 4,
            &(const anjay_dm_internal_attrs_t) {
                .standard = {
                    .min_period = 4,
                    .max_period = 99
                }
            },
            NULL));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 9, 4, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ, 9, 4, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 11, 11, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ, 11, 11, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));

    AVS_UNIT_ASSERT_NULL(get_fas(anjay)->objects);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, instance_default_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ2, 42, 2, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_NULL(get_fas(anjay)->objects);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ2, 3, 2,
            &(const anjay_dm_internal_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .min_period = 4,
                        .max_period = 9
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ2, 3, 5,
            &(const anjay_dm_internal_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .min_period = 7,
                        .max_period = 15
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ2, 9, 5,
            &(const anjay_dm_internal_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .min_period = 1,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ2, 14, 5,
            &(const anjay_dm_internal_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 10
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ2, 9, 5, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            3,
                            test_default_attrlist(
                                    test_default_attrs(
                                            2, 4, 9, ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            5, 7, 15,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            14,
                            test_default_attrlist(
                                    test_default_attrs(
                                            5, ANJAY_ATTRIB_PERIOD_NONE, 10,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    anjay_dm_internal_attrs_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_read_default_attrs(
            anjay, &OBJ2, 42, 2, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_read_default_attrs(
            anjay, &OBJ2, 3, 2, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_attrs_t) {
                               _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                   .min_period = 4,
                                   .max_period = 9
                               } });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_read_default_attrs(
            anjay, &OBJ2, 3, 5, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_attrs_t) {
                               _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                   .min_period = 7,
                                   .max_period = 15
                               } });
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_read_default_attrs(
            anjay, &OBJ2, 9, 5, &attrs, NULL));
    assert_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_read_default_attrs(
            anjay, &OBJ2, 14, 5, &attrs, NULL));
    assert_attrs_equal(&attrs,
                       &(const anjay_dm_internal_attrs_t) {
                               _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                   .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                   .max_period = 10
                               } });

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_resource_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_internal_res_attrs_t attrs;
    _anjay_mock_dm_expect_resource_read_attrs(
            anjay, &OBJ, 5, 6, 4, 0, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_resource_read_attrs(anjay, &OBJ, 5, 6, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY);

    _anjay_mock_dm_expect_resource_read_attrs(
            anjay, &OBJ, 5, 7, 42, -413, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL(_anjay_dm_resource_read_attrs(anjay, &OBJ, 5, 7, 42,
                                                        &attrs, NULL),
                          -413);

    _anjay_mock_dm_expect_resource_read_attrs(
            anjay, &OBJ, 7, 17, 4, 0,
            &(const anjay_dm_internal_res_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 77
                    },
                    .greater_than = 44.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = .5
                }
            });
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_resource_read_attrs(anjay, &OBJ, 7, 17, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs,
                           &(const anjay_dm_internal_res_attrs_t) {
                               .standard = {
                                   .common = {
                                       .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                       .max_period = 77
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
            &(const anjay_dm_internal_res_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 43,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 13.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ, 4, 9, 42,
            &(const anjay_dm_internal_res_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 43,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 13.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 4, 111, 7,
            &(const anjay_dm_internal_res_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 77
                    },
                    .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ, 4, 111, 7,
            &(const anjay_dm_internal_res_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 77,
                    },
                    .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE
                }
            },
            NULL));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 8, 9, 7,
            &(const anjay_dm_internal_res_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 0.8,
                    .less_than = 8.8,
                    .step = 88.8
                }
            },
            -8888);
    AVS_UNIT_ASSERT_EQUAL(
            _anjay_dm_resource_write_attrs(
                    anjay, &OBJ, 8, 9, 7,
                    &(const anjay_dm_internal_res_attrs_t) {
                        .standard = {
                            .common = {
                                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                .max_period = ANJAY_ATTRIB_PERIOD_NONE
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
            &(const anjay_dm_internal_res_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 4,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 99.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE,
                }
            },
            0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4,
            &(const anjay_dm_internal_res_attrs_t) {
                .standard = {
                    .common = {
                        .min_period = 4,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE
                    },
                    .greater_than = 99.0,
                    .less_than = ANJAY_ATTRIB_VALUE_NONE,
                    .step = ANJAY_ATTRIB_VALUE_NONE,
                }
            },
            NULL));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, NULL));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 11, 11, 11, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ, 11, 11, 11, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, NULL));

    AVS_UNIT_ASSERT_NULL(get_fas(anjay)->objects);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_resource_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;

    AVS_LIST_APPEND(&get_fas(anjay)->objects,
                    test_object_entry(
                            69, NULL,
                            test_instance_entry(
                                    3, NULL,
                                    test_resource_entry(
                                            1,
                                            test_resource_attrs(
                                                    42, 1, 2, 3.0, 4.0, 5.0,
                                                    ANJAY_DM_CON_ATTR_DEFAULT),
                                            NULL),
                                    NULL),
                            NULL));

    anjay_dm_internal_res_attrs_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_read_attrs(anjay, &OBJ2, 3, 1,
                                                          42, &attrs, NULL));
    assert_res_attrs_equal(
            &attrs,
            &(const anjay_dm_internal_res_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .common = {
                            .min_period = 1,
                            .max_period = 2
                        },
                        .greater_than = 3.0,
                        .less_than = 4.0,
                        .step = 5.0
                    } });
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_resource_read_attrs(anjay, &OBJ2, 3, 1, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_resource_read_attrs(anjay, &OBJ2, 3, 2, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_resource_read_attrs(anjay, &OBJ2, 2, 2, 4, &attrs, NULL));
    assert_res_attrs_equal(&attrs, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_resource_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 5, 3, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, NULL));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_NULL(get_fas(anjay)->objects);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 1,
            &(const anjay_dm_internal_res_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .common = {
                            .min_period = 1,
                            .max_period = ANJAY_ATTRIB_PERIOD_NONE
                        },
                        .greater_than = 34.0,
                        .less_than = ANJAY_ATTRIB_VALUE_NONE,
                        .step = ANJAY_ATTRIB_VALUE_NONE
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            NULL,
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            1,
                                            1,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            34.0,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 5, 3,
            &(const anjay_dm_internal_res_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .common = {
                            .min_period = 4,
                            .max_period = 5
                        },
                        .greater_than = 6.0,
                        .less_than = 7.0,
                        .step = 8.0
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 5,
            &(const anjay_dm_internal_res_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .common = {
                            .min_period = 9,
                            .max_period = 10
                        },
                        .greater_than = 11.0,
                        .less_than = 22.0,
                        .step = 33.0
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            NULL,
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            1,
                                            1,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            34.0,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_resource_attrs(
                                            5, 9, 10, 11.0, 22.0, 33.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    5,
                                    test_resource_attrs(
                                            3, 4, 5, 6.0, 7.0, 8.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ2, 2, 4,
            &(const anjay_dm_internal_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .min_period = 4,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 5, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 1, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            4, 4, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    5,
                                    test_resource_attrs(
                                            3, 4, 5, 6.0, 7.0, 8.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 5, 3, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            4, 4, ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 5,
            &(const anjay_dm_internal_res_attrs_t) {
                    _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                        .common = {
                            .min_period = 9,
                            .max_period = 10
                        },
                        .greater_than = 11.0,
                        .less_than = 22.0,
                        .step = 33.0
                    } },
            NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, &OBJ2, 2, 4, &ANJAY_DM_INTERNAL_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    69, NULL,
                    test_instance_entry(
                            2,
                            NULL,
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            5, 9, 10, 11.0, 22.0, 33.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(
            anjay, &OBJ2, 2, 3, 5, &ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_NULL(get_fas(anjay)->objects);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    DM_ATTR_STORAGE_TEST_FINISH;
}

//// SSID HANDLING /////////////////////////////////////////////////////////////

AVS_UNIT_TEST(attr_storage, ssid_it) {
    DM_ATTR_STORAGE_TEST_INIT;

    // server mapping:
    // /0/4/10 == 3
    // /0/7/10 == 2
    // /0/42/10 == 514
    // /0/514/10 == -4 (invalid)
    //
    // /1/9/0 == 514
    // /1/10/0 == 2
    // /1/11/0 == -5 (invalid)

    AVS_LIST_APPEND(
            &get_fas(anjay)->objects,
            test_object_entry(
                    42,
                    test_default_attrlist(
                            test_default_attrs(2, 5, 6,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            test_default_attrs(4, 7, 8,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            43, 101, 102,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            515, 103, 104,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            4, 109, 110, -0.1, -0.2, -0.3,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    2,
                                    test_resource_attrs(
                                            8, 111, 112, -0.4, -0.5, -0.6,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_resource_attrs(
                                            42, 113, 114, -0.7, -0.8, -0.9,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            42, 1, 2,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            514, 3, 4,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            3, 9, 10, -1.0, -2.0, -3.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    2,
                                    test_resource_attrs(
                                            7, 11, 12, -4.0, -5.0, -6.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_resource_attrs(
                                            42, 13, 14, -7.0, -8.0, -9.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    anjay_iid_t iid;
    void *cookie = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 514);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SECURITY2, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 514);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 1, 0, 7);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SECURITY2, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 7);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 2, 0, 42);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SECURITY2, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 42);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 3, 0, 4);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SECURITY2, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 4);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 4, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 4, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 4, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, 3));
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 42, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 42, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, 2));
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 7, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 7, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 514, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 514, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, -4));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SECURITY2, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    42,
                    test_default_attrlist(
                            test_default_attrs(2, 5, 6,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            514, 3, 4,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            3, 9, 10, -1.0, -2.0, -3.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    cookie = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 11);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SERVER, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 11);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 1, 0, 9);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SERVER, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 9);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 2, 0, 10);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SERVER, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_EQUAL(iid, 10);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 3, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 10, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 10, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, 2));
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 9, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 9, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 11, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 11, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, -5));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &FAKE_SERVER, &iid, &cookie, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    42,
                    test_default_attrlist(
                            test_default_attrs(2, 5, 6,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            514, 3, 4,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, ssid_remove) {
    DM_ATTR_STORAGE_TEST_INIT;

    AVS_LIST_APPEND(
            &get_fas(anjay)->objects,
            test_object_entry(
                    42,
                    test_default_attrlist(
                            test_default_attrs(2, 5, 6,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            test_default_attrs(4, 7, 8,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            43, 101, 102,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            515, 103, 104,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            4, 109, 110, -0.1, -0.2, -0.3,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    2,
                                    test_resource_attrs(
                                            8, 111, 112, -0.4, -0.5, -0.6,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_resource_attrs(
                                            42, 113, 114, -0.7, -0.8, -0.9,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            42, 1, 2,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            514, 3, 4,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            3, 9, 10, -1.0, -2.0, -3.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    2,
                                    test_resource_attrs(
                                            7, 11, 12, -4.0, -5.0, -6.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_resource_attrs(
                                            42, 13, 14, -7.0, -8.0, -9.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 7, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 7, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, 2));
    _anjay_mock_dm_expect_instance_remove(anjay, &FAKE_SECURITY2, 7, 0);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_remove(anjay, &FAKE_SECURITY2, 7, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    42,
                    test_default_attrlist(
                            test_default_attrs(4, 7, 8,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            43, 101, 102,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            515, 103, 104,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            4, 109, 110, -0.1, -0.2, -0.3,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    2,
                                    test_resource_attrs(
                                            8, 111, 112, -0.4, -0.5, -0.6,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_resource_attrs(
                                            42, 113, 114, -0.7, -0.8, -0.9,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            42, 1, 2,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            514, 3, 4,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            3, 9, 10, -1.0, -2.0, -3.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    2,
                                    test_resource_attrs(
                                            7, 11, 12, -4.0, -5.0, -6.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_resource_attrs(
                                            42, 13, 14, -7.0, -8.0, -9.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 19, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 19, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, 42));
    _anjay_mock_dm_expect_instance_remove(anjay, &FAKE_SERVER, 19, 0);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_remove(anjay, &FAKE_SERVER, 19, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(
                    42,
                    test_default_attrlist(
                            test_default_attrs(4, 7, 8,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            NULL),
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            43, 101, 102,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    test_default_attrs(
                                            515, 103, 104,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            4, 109, 110, -0.1, -0.2, -0.3,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    2,
                                    test_resource_attrs(
                                            8, 111, 112, -0.4, -0.5, -0.6,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    test_instance_entry(
                            2,
                            test_default_attrlist(
                                    test_default_attrs(
                                            514, 3, 4,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    1,
                                    test_resource_attrs(
                                            3, 9, 10, -1.0, -2.0, -3.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    2,
                                    test_resource_attrs(
                                            7, 11, 12, -4.0, -5.0, -6.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, nested_iterations) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_iid_t iid;

    // prepare initial state
    AVS_LIST_APPEND(
            &get_fas(anjay)->objects,
            test_object_entry(42, NULL, test_instance_entry(1, NULL, NULL),
                              test_instance_entry(2, NULL, NULL),
                              test_instance_entry(3, NULL, NULL),
                              test_instance_entry(4, NULL, NULL),
                              test_instance_entry(5, NULL, NULL), NULL));

    void *cookie1 = NULL;
    void *cookie2 = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie2, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie2, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie2, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie2, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(anjay));
    get_fas(anjay)->modified_since_persist = false;
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(42, NULL, test_instance_entry(1, NULL, NULL),
                              test_instance_entry(2, NULL, NULL),
                              test_instance_entry(3, NULL, NULL), NULL));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, parallel_iterations) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_iid_t iid;

    // prepare initial state
    AVS_LIST_APPEND(
            &get_fas(anjay)->objects,
            test_object_entry(42, NULL, test_instance_entry(1, NULL, NULL),
                              test_instance_entry(2, NULL, NULL),
                              test_instance_entry(3, NULL, NULL),
                              test_instance_entry(4, NULL, NULL),
                              test_instance_entry(5, NULL, NULL), NULL));

    void *cookie1 = NULL;
    void *cookie2 = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie2, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie2, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie2, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie1, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_dm_instance_it(anjay, &OBJ, &iid, &cookie2, NULL));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(get_fas(anjay)->objects), 1);
    assert_object_equal(
            get_fas(anjay)->objects,
            test_object_entry(42, NULL, test_instance_entry(1, NULL, NULL),
                              test_instance_entry(2, NULL, NULL),
                              test_instance_entry(3, NULL, NULL),
                              test_instance_entry(4, NULL, NULL),
                              test_instance_entry(5, NULL, NULL), NULL));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(anjay));

    DM_ATTR_STORAGE_TEST_FINISH;
}

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

static const anjay_dm_attributes_t *FAKE_DM_ATTRS =
        (anjay_dm_attributes_t *) -1;
static const anjay_dm_resource_attributes_t *FAKE_DM_RES_ATTRS =
        (anjay_dm_resource_attributes_t *) -1;

AVS_UNIT_TEST(set_attribs, fail_on_invalid_ssid) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ_NOATTRS, &FAKE_SECURITY2);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay));

    const anjay_ssid_t SSIDS_TO_TEST[] = { ANJAY_SSID_ANY, ANJAY_SSID_BOOTSTRAP,
                                           341 };
    // Assumming no Security Instances
    for (int i = 0; i < (int) AVS_ARRAY_SIZE(SSIDS_TO_TEST); ++i) {
        // object
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0,
                                              ANJAY_IID_INVALID);
        }
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_object_attrs(
                anjay, SSIDS_TO_TEST[i], OBJ_NOATTRS->oid, FAKE_DM_ATTRS));

        // instance
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0,
                                              ANJAY_IID_INVALID);
        }
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_instance_attrs(
                anjay, SSIDS_TO_TEST[i], OBJ_NOATTRS->oid, 0, FAKE_DM_ATTRS));

        // resource
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0,
                                              ANJAY_IID_INVALID);
        }
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
                anjay, SSIDS_TO_TEST[i], OBJ_NOATTRS->oid, 0, 0,
                FAKE_DM_RES_ATTRS));
    }

    // Assumming one Security Instance, but Bootstrap
    for (int i = 0; i < (int) AVS_ARRAY_SIZE(SSIDS_TO_TEST); ++i) {
        // object
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 1);
            _anjay_mock_dm_expect_resource_present(
                    anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_BOOTSTRAP,
                    1);
            _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                                ANJAY_DM_RID_SECURITY_BOOTSTRAP,
                                                0, ANJAY_MOCK_DM_BOOL(0, true));

            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 1, 0,
                                              ANJAY_IID_INVALID);
            _anjay_mock_dm_expect_resource_present(
                    anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_SSID, 0);
        }
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_object_attrs(
                anjay, SSIDS_TO_TEST[i], OBJ_NOATTRS->oid, FAKE_DM_ATTRS));

        // instance
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 1);
            _anjay_mock_dm_expect_resource_present(
                    anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_BOOTSTRAP,
                    1);
            _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                                ANJAY_DM_RID_SECURITY_BOOTSTRAP,
                                                0, ANJAY_MOCK_DM_BOOL(0, true));

            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 1, 0,
                                              ANJAY_IID_INVALID);
            _anjay_mock_dm_expect_resource_present(
                    anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_SSID, 0);
        }
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_instance_attrs(
                anjay, SSIDS_TO_TEST[i], OBJ_NOATTRS->oid, 0, FAKE_DM_ATTRS));

        // resource
        // attempt to query SSID
        if (SSIDS_TO_TEST[i] != ANJAY_SSID_BOOTSTRAP) {
            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 1);
            _anjay_mock_dm_expect_resource_present(
                    anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_BOOTSTRAP,
                    1);
            _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                                ANJAY_DM_RID_SECURITY_BOOTSTRAP,
                                                0, ANJAY_MOCK_DM_BOOL(0, true));

            _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 1, 0,
                                              ANJAY_IID_INVALID);
            _anjay_mock_dm_expect_resource_present(
                    anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_SSID, 0);
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

    AVS_UNIT_ASSERT_FAILED(
            anjay_attr_storage_set_object_attrs(anjay, 1, 5, FAKE_DM_ATTRS));
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_instance_attrs(
            anjay, 1, 5, 1, FAKE_DM_ATTRS));
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
            anjay, 1, 5, 1, 0, FAKE_DM_RES_ATTRS));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(set_attribs, fail_on_invalid_iid) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ_NOATTRS, &FAKE_SECURITY2);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay));

    // attempt to query SSID
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_BOOTSTRAP, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_BOOTSTRAP, 0,
                                        ANJAY_MOCK_DM_BOOL(0, false));

    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));

    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_instance_attrs(
            anjay, 1, OBJ_NOATTRS->oid, ANJAY_IID_INVALID, FAKE_DM_ATTRS));

    // attempt to query SSID
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_BOOTSTRAP, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_BOOTSTRAP, 0,
                                        ANJAY_MOCK_DM_BOOL(0, false));

    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));

    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
            anjay, 1, OBJ_NOATTRS->oid, ANJAY_IID_INVALID, 1,
            FAKE_DM_RES_ATTRS));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(set_attribs, fail_on_invalid_rid) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ_NOATTRS, &FAKE_SECURITY2);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay));

    // attempt to query SSID
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_BOOTSTRAP, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_BOOTSTRAP, 0,
                                        ANJAY_MOCK_DM_BOOL(0, false));

    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));

    _anjay_mock_dm_expect_instance_present(anjay, &OBJ_NOATTRS, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ_NOATTRS, 1, 1, 0);
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_set_resource_attrs(
            anjay, 1, OBJ_NOATTRS->oid, 1, 1, FAKE_DM_RES_ATTRS));

    DM_TEST_FINISH;
}
