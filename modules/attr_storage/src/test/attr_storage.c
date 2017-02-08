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

#include <avsystem/commons/unit/test.h>

#include <anjay/attr_storage.h>
#include <anjay/anjay.h>

#include <anjay_modules/dm/execute.h>

#include <anjay_test/dm.h>

#include "attr_storage_test.h"

#include <string.h>

//// OBJECT HANDLING ///////////////////////////////////////////////////////////
static int fail() {
    return -1;
}

static fas_object_t *
find_and_assert_wrapper_object(anjay_attr_storage_t *fas,
                               const anjay_dm_object_def_t *const *backend) {
    fas_object_t *wrapper =
            _anjay_attr_storage_find_object(fas, (*backend)->oid);

    AVS_UNIT_ASSERT_NOT_NULL(wrapper);

    AVS_UNIT_ASSERT_TRUE(wrapper->def_ptr == &wrapper->def);
    AVS_UNIT_ASSERT_TRUE(wrapper->backend == backend);
    AVS_UNIT_ASSERT_NULL(wrapper->instance_it_iids);
    AVS_UNIT_ASSERT_NULL(wrapper->default_attrs);
    AVS_UNIT_ASSERT_NULL(wrapper->instances);

    return wrapper;
}

static void assert_objdef_equal(const anjay_dm_object_def_t *actual,
                                const anjay_dm_object_def_t *expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->oid, expected->oid);
    AVS_UNIT_ASSERT_EQUAL(actual->rid_bound, expected->rid_bound);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            &actual->object_read_default_attrs,
            &expected->object_read_default_attrs,
            sizeof(anjay_dm_object_def_t)
                    - offsetof(anjay_dm_object_def_t,
                               object_read_default_attrs));
}

const anjay_dm_object_def_t *const NO_ATTRS = &(const anjay_dm_object_def_t) {
    .oid = 514,
    .rid_bound = 42,
    .instance_it = (anjay_dm_instance_it_t *) fail,
    .instance_present = (anjay_dm_instance_present_t *) fail,
    .resource_read = (anjay_dm_resource_read_t *) fail
};

static void test_wrap_no_attrs(anjay_attr_storage_t *fas) {
    size_t sz = AVS_LIST_SIZE(fas->objects);
    const anjay_dm_object_def_t *const *wrapped =
            anjay_attr_storage_wrap_object(fas, &NO_ATTRS);
    AVS_UNIT_ASSERT_NOT_NULL(wrapped);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(fas->objects), sz + 1);
    fas_object_t *obj = find_and_assert_wrapper_object(fas, &NO_ATTRS);
    AVS_UNIT_ASSERT_TRUE(wrapped == &obj->def_ptr);

    AVS_UNIT_ASSERT_FALSE(
            implements_any_object_default_attrs_handlers(NO_ATTRS));
    AVS_UNIT_ASSERT_FALSE(
            implements_any_instance_default_attrs_handlers(NO_ATTRS));
    AVS_UNIT_ASSERT_FALSE(implements_any_resource_attrs_handlers(NO_ATTRS));
    assert_objdef_equal(&obj->def,
                        &(const anjay_dm_object_def_t) {
                            .oid = 514,
                            .rid_bound = 42,
                            .object_read_default_attrs =
                                    object_read_default_attrs,
                            .object_write_default_attrs =
                                    object_write_default_attrs,
                            .instance_it = instance_it,
                            .instance_present = instance_present,
                            .instance_read_default_attrs =
                                    instance_read_default_attrs,
                            .instance_write_default_attrs =
                                    instance_write_default_attrs,
                            .resource_read = resource_read,
                            .resource_read_attrs = resource_read_attrs,
                            .resource_write_attrs = resource_write_attrs
                        });

    AVS_UNIT_ASSERT_NULL(anjay_attr_storage_wrap_object(fas, &NO_ATTRS));
}

const anjay_dm_object_def_t *const OBJ_READ = &(const anjay_dm_object_def_t) {
    .oid = 42,
    .rid_bound = 13,
    .object_read_default_attrs =
            (anjay_dm_object_read_default_attrs_t *) fail,
    .instance_present = (anjay_dm_instance_present_t *) fail,
    .instance_create = (anjay_dm_instance_create_t *) fail,
    .resource_write = (anjay_dm_resource_write_t *) fail,
    .resource_execute = (anjay_dm_resource_execute_t *) fail
};

static void test_wrap_obj_read(anjay_attr_storage_t *fas) {
    size_t sz = AVS_LIST_SIZE(fas->objects);
    const anjay_dm_object_def_t *const *wrapped =
            anjay_attr_storage_wrap_object(fas, &OBJ_READ);
    AVS_UNIT_ASSERT_NOT_NULL(wrapped);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(fas->objects), sz + 1);
    fas_object_t *obj = find_and_assert_wrapper_object(fas, &OBJ_READ);
    AVS_UNIT_ASSERT_TRUE(wrapped == &obj->def_ptr);

    AVS_UNIT_ASSERT_TRUE(
            implements_any_object_default_attrs_handlers(OBJ_READ));
    AVS_UNIT_ASSERT_FALSE(
            implements_any_instance_default_attrs_handlers(OBJ_READ));
    AVS_UNIT_ASSERT_FALSE(implements_any_resource_attrs_handlers(OBJ_READ));
    assert_objdef_equal(&obj->def,
                        &(const anjay_dm_object_def_t) {
                            .oid = 42,
                            .rid_bound = 13,
                            .object_read_default_attrs =
                                    object_read_default_attrs,
                            .instance_present = instance_present,
                            .instance_create = instance_create,
                            .instance_read_default_attrs =
                                    instance_read_default_attrs,
                            .instance_write_default_attrs =
                                    instance_write_default_attrs,
                            .resource_write = resource_write,
                            .resource_execute = resource_execute,
                            .resource_read_attrs = resource_read_attrs,
                            .resource_write_attrs = resource_write_attrs
                        });

    AVS_UNIT_ASSERT_NULL(anjay_attr_storage_wrap_object(fas, &OBJ_READ));
}

const anjay_dm_object_def_t *const
INSTANCE_WRITE_AND_RESOURCE = &(const anjay_dm_object_def_t) {
    .oid = 69,
    .rid_bound = 4,
    .instance_remove = (anjay_dm_instance_remove_t *) fail,
    .instance_write_default_attrs =
            (anjay_dm_instance_write_default_attrs_t *) fail,
    .resource_present = (anjay_dm_resource_present_t *) fail,
    .resource_read = (anjay_dm_resource_read_t *) fail,
    .resource_dim = (anjay_dm_resource_dim_t *) fail,
    .resource_read_attrs = (anjay_dm_resource_read_attrs_t *) fail,
    .resource_write_attrs = (anjay_dm_resource_write_attrs_t *) fail
};

static void test_instance_write_and_resource(anjay_attr_storage_t *fas) {
    size_t sz = AVS_LIST_SIZE(fas->objects);
    const anjay_dm_object_def_t *const *wrapped =
            anjay_attr_storage_wrap_object(fas, &INSTANCE_WRITE_AND_RESOURCE);
    AVS_UNIT_ASSERT_NOT_NULL(wrapped);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(fas->objects), sz + 1);
    fas_object_t *obj =
            find_and_assert_wrapper_object(fas, &INSTANCE_WRITE_AND_RESOURCE);
    AVS_UNIT_ASSERT_TRUE(wrapped == &obj->def_ptr);

    AVS_UNIT_ASSERT_FALSE(implements_any_object_default_attrs_handlers(
            INSTANCE_WRITE_AND_RESOURCE));
    AVS_UNIT_ASSERT_TRUE(implements_any_instance_default_attrs_handlers(
            INSTANCE_WRITE_AND_RESOURCE));
    AVS_UNIT_ASSERT_TRUE(implements_any_resource_attrs_handlers(
            INSTANCE_WRITE_AND_RESOURCE));
    assert_objdef_equal(&obj->def,
                        &(const anjay_dm_object_def_t) {
                            .oid = 69,
                            .rid_bound = 4,
                            .object_read_default_attrs =
                                    object_read_default_attrs,
                            .object_write_default_attrs =
                                    object_write_default_attrs,
                            .instance_remove = instance_remove,
                            .instance_write_default_attrs =
                                    instance_write_default_attrs,
                            .resource_present = resource_present,
                            .resource_read = resource_read,
                            .resource_dim = resource_dim,
                            .resource_read_attrs = resource_read_attrs,
                            .resource_write_attrs = resource_write_attrs
                        });

    AVS_UNIT_ASSERT_NULL(anjay_attr_storage_wrap_object(fas, &OBJ_READ));
}

static anjay_t *const FAKE_ANJAY = (anjay_t *) ~(uintptr_t) NULL;

AVS_UNIT_TEST(attr_storage, wrap_object) {
    anjay_attr_storage_t *fas = anjay_attr_storage_new(FAKE_ANJAY);
    AVS_UNIT_ASSERT_NOT_NULL(fas);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(fas->objects), 0);

    AVS_UNIT_ASSERT_NULL(anjay_attr_storage_wrap_object(fas, NULL));
    test_wrap_no_attrs(fas);
    test_wrap_obj_read(fas);
    test_instance_write_and_resource(fas);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    anjay_attr_storage_delete(fas);
}

//// PASSIVE PROXY HANDLERS ////////////////////////////////////////////////////

static const anjay_dm_object_def_t *const OBJ2 =
        &(const anjay_dm_object_def_t) {
            .oid = 69,
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

#define DM_ATTR_STORAGE_TEST_INIT \
        anjay_attr_storage_t *fas = anjay_attr_storage_new(FAKE_ANJAY); \
        const anjay_dm_object_def_t *const *WRAPPED_OBJ = \
                anjay_attr_storage_wrap_object(fas, &OBJ); \
        const anjay_dm_object_def_t *const *WRAPPED_OBJ2 = \
                anjay_attr_storage_wrap_object(fas, &OBJ2); \
        const anjay_dm_object_def_t *const *WRAPPED_SECURITY = \
                anjay_attr_storage_wrap_object(fas, &FAKE_SECURITY2); \
        const anjay_dm_object_def_t *const *WRAPPED_SERVER = \
                anjay_attr_storage_wrap_object(fas, &FAKE_SERVER); \
        DM_TEST_INIT_WITH_OBJECTS(WRAPPED_OBJ, \
                                  WRAPPED_OBJ2, \
                                  WRAPPED_SECURITY, \
                                  WRAPPED_SERVER)

#define DM_ATTR_STORAGE_TEST_FINISH do { \
    (void) mocksocks; \
    DM_TEST_FINISH; \
    anjay_attr_storage_delete(fas); \
} while (0)

AVS_UNIT_TEST(attr_storage, instance_create) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_iid_t iid = 42;
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 42, 1, 0, 42);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_create(anjay, WRAPPED_OBJ,
                                                            &iid, 1));
    AVS_UNIT_ASSERT_EQUAL(iid, 42);
    iid = 0;
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 0, 1, -42, 69);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_create(anjay, WRAPPED_OBJ,
                                                          &iid, 1), -42);
    AVS_UNIT_ASSERT_EQUAL(iid, 69);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_read) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 514, 42, 0,
                                        ANJAY_MOCK_DM_NONE);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_read(anjay, WRAPPED_OBJ,
                                                          514, 42, NULL));
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 14, -7,
                                        ANJAY_MOCK_DM_NONE);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_read(anjay, WRAPPED_OBJ,
                                                        69, 14, NULL), -7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_write) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 42,
                                         ANJAY_MOCK_DM_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_write(anjay, WRAPPED_OBJ,
                                                           514, 42, NULL));
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 14,
                                         ANJAY_MOCK_DM_NONE, -7);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_write(anjay, WRAPPED_OBJ,
                                                         69, 14, NULL), -7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_execute) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_execute_ctx_t *ctx = _anjay_execute_ctx_create(NULL);
    AVS_UNIT_ASSERT_NOT_NULL(ctx);
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 514, 42,
                                           ANJAY_MOCK_DM_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_execute(anjay, WRAPPED_OBJ,
                                                             514, 42, ctx));
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 69, 14,
                                           ANJAY_MOCK_DM_NONE, -7);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_execute(anjay, WRAPPED_OBJ,
                                                           69, 14, ctx), -7);
    _anjay_execute_ctx_destroy(&ctx);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_dim) {
    DM_ATTR_STORAGE_TEST_INIT;
    _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 514, 42, 17);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_dim(anjay, WRAPPED_OBJ,
                                                       514, 42), 17);
    _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 69, 14, -7);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_dim(anjay, WRAPPED_OBJ,
                                                       69, 14), -7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

//// ACTIVE PROXY HANDLERS /////////////////////////////////////////////////////

AVS_UNIT_TEST(attr_storage, instance_it) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_iid_t iid;
    void *cookie = NULL;

    // prepare initial state
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    1,
                    test_default_attrlist(
                            test_default_attrs(0, 2, 514),
                            test_default_attrs(4, 1, ANJAY_ATTRIB_PERIOD_NONE),
                            NULL),
                    test_resource_entry(
                            3,
                            test_resource_attrs(1,
                                                ANJAY_ATTRIB_PERIOD_NONE,
                                                ANJAY_ATTRIB_PERIOD_NONE,
                                                42.0,
                                                14.0,
                                                3.0),
                            NULL),
                    test_resource_entry(7, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(0, 42, 44),
                            test_default_attrs(7, 33, 888),
                            NULL),
                    test_resource_entry(2, NULL),
                    test_resource_entry(
                            4,
                            test_resource_attrs(4, 1, 2, 3.0, 4.0, 5.0),
                            NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(4, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(7, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    8,
                    test_default_attrlist(
                            test_default_attrs(0, 0, 0),
                            NULL),
                    test_resource_entry(3, NULL),
                    NULL));

    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 7);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 7);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 13);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 13);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 4, 0, 42);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 42);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 5, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            2);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(0, 42, 44),
                            test_default_attrs(7, 33, 888),
                            NULL),
                    test_resource_entry(2, NULL),
                    test_resource_entry(
                            4,
                            test_resource_attrs(4, 1, 2, 3.0, 4.0, 5.0),
                            NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_find_object(fas, 42)->instances),
            test_instance_entry(7, NULL, NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));

    // error
    fas->modified_since_persist = false;
    cookie = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, -11, 7);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                      &iid, &cookie), -11);
    AVS_UNIT_ASSERT_EQUAL(iid, 7);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, instance_present) {
    DM_ATTR_STORAGE_TEST_INIT;

    // prepare initial state
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    4, NULL,
                    test_resource_entry(33, NULL),
                    test_resource_entry(69, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    7, NULL,
                    test_resource_entry(11, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    21, NULL,
                    test_resource_entry(22, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    42, NULL,
                    test_resource_entry(17, NULL),
                    NULL));

    // tests
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 42, 1);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_present(anjay, WRAPPED_OBJ,
                                                           42), 1);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            4);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 21, -1);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_present(anjay, WRAPPED_OBJ,
                                                           21), -1);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            4);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 4, 0);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_present(anjay, WRAPPED_OBJ,
                                                           4), 0);

    // verification
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            3);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    7, NULL,
                    test_resource_entry(11, NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_find_object(fas, 42)->instances),
            test_instance_entry(
                    21, NULL,
                    test_resource_entry(22, NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NTH(_anjay_attr_storage_find_object(fas, 42)->instances,
                         2),
            test_instance_entry(
                    42, NULL,
                    test_resource_entry(17, NULL),
                    NULL));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, instance_remove) {
    DM_ATTR_STORAGE_TEST_INIT;

    // prepare initial state
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    4, NULL,
                    test_resource_entry(33, NULL),
                    test_resource_entry(69, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    7, NULL,
                    test_resource_entry(11, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    42, NULL,
                    test_resource_entry(17, NULL),
                    NULL));

    // tests
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 42, 0);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_remove(anjay, WRAPPED_OBJ,
                                                          42), 0);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            2);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 2, 0);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_remove(anjay, WRAPPED_OBJ,
                                                          2), 0);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            2);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 7, -44);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_remove(anjay, WRAPPED_OBJ,
                                                          7), -44);

    // verification
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            2);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    4, NULL,
                    test_resource_entry(33, NULL),
                    test_resource_entry(69, NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_find_object(fas, 42)->instances),
            test_instance_entry(
                    7, NULL,
                    test_resource_entry(11, NULL),
                    NULL));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, resource_present) {
    DM_ATTR_STORAGE_TEST_INIT;

    // prepare initial state
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    4, NULL,
                    test_resource_entry(11, NULL),
                    test_resource_entry(33, NULL),
                    test_resource_entry(69, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    7, NULL,
                    test_resource_entry(11, NULL),
                    test_resource_entry(42, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    21, NULL,
                    test_resource_entry(22, NULL),
                    test_resource_entry(33, NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    42, NULL,
                    test_resource_entry(17, NULL),
                    test_resource_entry(69, NULL),
                    NULL));

    const anjay_iid_t iid_any = 0;
    // tests
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, iid_any, 42, 1);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_present(anjay, WRAPPED_OBJ,
                                                           iid_any, 42), 1);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, iid_any, 17, -1);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_present(anjay, WRAPPED_OBJ,
                                                           iid_any, 17), -1);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, iid_any, 33, 0);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_present(anjay, WRAPPED_OBJ,
                                                           iid_any, 33), 0);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, iid_any, 69, 0);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_present(anjay, WRAPPED_OBJ,
                                                           iid_any, 69), 0);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            4);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, iid_any, 11, 0);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_present(anjay, WRAPPED_OBJ,
                                                           iid_any, 11), 0);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));

    // verification
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            3);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    7, NULL,
                    test_resource_entry(42, NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_find_object(fas, 42)->instances),
            test_instance_entry(
                    21, NULL,
                    test_resource_entry(22, NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NTH(_anjay_attr_storage_find_object(fas, 42)->instances,
                         2),
            test_instance_entry(
                    42, NULL,
                    test_resource_entry(17, NULL),
                    NULL));
    DM_ATTR_STORAGE_TEST_FINISH;
}

//// ATTRIBUTE HANDLERS ////////////////////////////////////////////////////////

AVS_UNIT_TEST(attr_storage, read_object_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_attributes_t attrs;
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 4, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->object_read_default_attrs(
            anjay, WRAPPED_OBJ, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));

    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 42, -413,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->object_read_default_attrs(
            anjay, WRAPPED_OBJ, 42, &attrs), -413);

    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 7, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77,
            });
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->object_read_default_attrs(
            anjay, WRAPPED_OBJ, 7, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs,
                                      (&(const anjay_dm_attributes_t) {
                                          .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                          .max_period = 77
                                      }), sizeof(attrs));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_object_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 42,
            &(const anjay_dm_attributes_t) {
                .min_period = 43,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->object_write_default_attrs(
            anjay, WRAPPED_OBJ, 42,
            &(const anjay_dm_attributes_t) {
                .min_period = 43,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->object_write_default_attrs(
            anjay, WRAPPED_OBJ, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 8,
            &(const anjay_dm_attributes_t) {
                .min_period = 88,
                .max_period = 888,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }, -8888);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->object_write_default_attrs(
            anjay, WRAPPED_OBJ, 8,
            &(const anjay_dm_attributes_t) {
                .min_period = 88,
                .max_period = 888,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }), -8888);

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 9,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = 99,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->object_write_default_attrs(
            anjay, WRAPPED_OBJ, 9,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = 99,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 9, &ANJAY_DM_ATTRIBS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->object_write_default_attrs(
            anjay, WRAPPED_OBJ, 9, &ANJAY_DM_ATTRIBS_EMPTY));

    _anjay_mock_dm_expect_object_write_default_attrs(
            anjay, &OBJ, 11, &ANJAY_DM_ATTRIBS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->object_write_default_attrs(
            anjay, WRAPPED_OBJ, 11, &ANJAY_DM_ATTRIBS_EMPTY));

    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, object_default_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));

    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_write_default_attrs(
            anjay, WRAPPED_OBJ2, 42,
            &(const anjay_dm_attributes_t) {
                .min_period = 43,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_write_default_attrs(
            anjay, WRAPPED_OBJ2, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_write_default_attrs(
            anjay, WRAPPED_OBJ2, 8, &ANJAY_DM_ATTRIBS_EMPTY));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_write_default_attrs(
            anjay, WRAPPED_OBJ2, 9,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = 99,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_write_default_attrs(
            anjay, WRAPPED_OBJ2, 11, &ANJAY_DM_ATTRIBS_EMPTY));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_write_default_attrs(
            anjay, WRAPPED_OBJ2, 9, &ANJAY_DM_ATTRIBS_EMPTY));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(
            _anjay_attr_storage_find_object(fas, 69)->default_attrs), 2);
    assert_attrs_equal(
            _anjay_attr_storage_find_object(fas, 69)->default_attrs,
            test_default_attrs(7, ANJAY_ATTRIB_PERIOD_NONE, 77));
    assert_attrs_equal(
            AVS_LIST_NEXT(
                    _anjay_attr_storage_find_object(fas, 69)->default_attrs),
            test_default_attrs(42, 43, ANJAY_ATTRIB_PERIOD_NONE));

    anjay_dm_attributes_t attrs;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_read_default_attrs(
            anjay, WRAPPED_OBJ2, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_read_default_attrs(
            anjay, WRAPPED_OBJ2, 42, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs,
                                      (&(const anjay_dm_attributes_t) {
                                          .min_period = 43,
                                          .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                                          .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .less_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .step = ANJAY_ATTRIB_VALUE_NONE
                                      }), sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->object_read_default_attrs(
            anjay, WRAPPED_OBJ2, 7, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs,
                                      (&(const anjay_dm_attributes_t) {
                                          .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                          .max_period = 77,
                                          .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .less_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .step = ANJAY_ATTRIB_VALUE_NONE
                                      }), sizeof(attrs));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_instance_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_attributes_t attrs;
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 5, 4, 0,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_read_default_attrs(
            anjay, WRAPPED_OBJ, 5, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));

    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 5, 42, -413,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_read_default_attrs(
            anjay, WRAPPED_OBJ, 5, 42, &attrs), -413);

    _anjay_mock_dm_expect_instance_read_default_attrs(
            anjay, &OBJ, 7, 4, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77
            });
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_read_default_attrs(
            anjay, WRAPPED_OBJ, 7, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs,
                                      (&(const anjay_dm_attributes_t) {
                                          .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                          .max_period = 77
                                      }), sizeof(attrs));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_instance_default_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 4, 42,
            &(const anjay_dm_attributes_t) {
                .min_period = 43,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ, 4, 42,
            &(const anjay_dm_attributes_t) {
                .min_period = 43,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE
            }));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 4, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ, 4, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77
            }));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 8, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = 88,
                .max_period = 888
            }, -8888);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ, 8, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = 88,
                .max_period = 888
            }), -8888);

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 9, 4,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = 99
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ, 9, 4,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = 99
            }));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 9, 4, &ANJAY_DM_ATTRIBS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ, 9, 4, &ANJAY_DM_ATTRIBS_EMPTY));

    _anjay_mock_dm_expect_instance_write_default_attrs(
            anjay, &OBJ, 11, 11, &ANJAY_DM_ATTRIBS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ, 11, 11, &ANJAY_DM_ATTRIBS_EMPTY));

    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, instance_default_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));

    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ2, 42, 2, &ANJAY_DM_ATTRIBS_EMPTY));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_find_object(fas, 69)->instances);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ2, 3, 2,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = 9,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ2, 3, 5,
            &(const anjay_dm_attributes_t) {
                .min_period = 7,
                .max_period = 15,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ2, 9, 5,
            &(const anjay_dm_attributes_t) {
                .min_period = 1,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ2, 14, 5,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 10,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ2, 9, 5, &ANJAY_DM_ATTRIBS_EMPTY));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 69)->instances),
            2);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 69)->instances,
            test_instance_entry(
                    3,
                    test_default_attrlist(
                            test_default_attrs(2, 4, 9),
                            test_default_attrs(5, 7, 15),
                            NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_find_object(fas, 69)->instances),
            test_instance_entry(
                    14,
                    test_default_attrlist(
                            test_default_attrs(5, ANJAY_ATTRIB_PERIOD_NONE, 10),
                            NULL),
                    NULL));

    anjay_dm_attributes_t attrs;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_read_default_attrs(
            anjay, WRAPPED_OBJ2, 42, 2, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_read_default_attrs(
            anjay, WRAPPED_OBJ2, 3, 2, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs,
                                      (&(const anjay_dm_attributes_t) {
                                          .min_period = 4,
                                          .max_period = 9,
                                          .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .less_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .step = ANJAY_ATTRIB_VALUE_NONE
                                      }), sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_read_default_attrs(
            anjay, WRAPPED_OBJ2, 3, 5, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs,
                                      (&(const anjay_dm_attributes_t) {
                                          .min_period = 7,
                                          .max_period = 15,
                                          .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .less_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .step = ANJAY_ATTRIB_VALUE_NONE
                                      }), sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_read_default_attrs(
            anjay, WRAPPED_OBJ2, 9, 5, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_read_default_attrs(
            anjay, WRAPPED_OBJ2, 14, 5, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs,
                                      (&(const anjay_dm_attributes_t) {
                                          .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                          .max_period = 10,
                                          .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .less_than = ANJAY_ATTRIB_VALUE_NONE,
                                          .step = ANJAY_ATTRIB_VALUE_NONE
                                      }), sizeof(attrs));

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_resource_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    anjay_dm_attributes_t attrs;
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 5, 6, 4, 0,
                                              &ANJAY_DM_ATTRIBS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_read_attrs(
            anjay, WRAPPED_OBJ, 5, 6, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));

    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 5, 7, 42, -413,
                                              &ANJAY_DM_ATTRIBS_EMPTY);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_read_attrs(
            anjay, WRAPPED_OBJ, 5, 7, 42, &attrs), -413);

    _anjay_mock_dm_expect_resource_read_attrs(
            anjay, &OBJ, 7, 17, 4, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77,
                .greater_than = 44.0,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = .5
            });
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_read_attrs(
            anjay, WRAPPED_OBJ, 7, 17, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            &attrs,
            (&(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77,
                .greater_than = 44.0,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = .5
            }), sizeof(attrs));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_resource_attrs_proxy) {
    DM_ATTR_STORAGE_TEST_INIT;

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 4, 9, 42,
            &(const anjay_dm_attributes_t) {
                .min_period = 43,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = 13.0,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_write_attrs(
            anjay, WRAPPED_OBJ, 4, 9, 42,
            &(const anjay_dm_attributes_t) {
                .min_period = 43,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = 13.0,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 4, 111, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_write_attrs(
            anjay, WRAPPED_OBJ, 4, 111, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 77,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 8, 9, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = 0.8,
                .less_than = 8.8,
                .step = 88.8
            }, -8888);
    AVS_UNIT_ASSERT_EQUAL((*WRAPPED_OBJ)->resource_write_attrs(
            anjay, WRAPPED_OBJ, 8, 9, 7,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = 0.8,
                .less_than = 8.8,
                .step = 88.8
            }), -8888);

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = 99.0,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE,
            }, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_write_attrs(
            anjay, WRAPPED_OBJ, 9, 23, 4,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = 99.0,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE,
            }));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 9, 23, 4, &ANJAY_DM_ATTRIBS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_write_attrs(
            anjay, WRAPPED_OBJ, 9, 23, 4, &ANJAY_DM_ATTRIBS_EMPTY));

    _anjay_mock_dm_expect_resource_write_attrs(
            anjay, &OBJ, 11, 11, 11, &ANJAY_DM_ATTRIBS_EMPTY, 0);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->resource_write_attrs(
            anjay, WRAPPED_OBJ, 11, 11, 11, &ANJAY_DM_ATTRIBS_EMPTY));

    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, read_resource_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;

    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 69)->instances,
            test_instance_entry(
                    3, NULL,
                    test_resource_entry(
                            1,
                            test_resource_attrs(42, 1, 2, 3.0, 4.0, 5.0),
                            NULL),
                    NULL));

    anjay_dm_attributes_t attrs;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_read_attrs(
            anjay, WRAPPED_OBJ2, 3, 1, 42, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs,
            (&(const anjay_dm_attributes_t) {
                .min_period = 1,
                .max_period = 2,
                .greater_than = 3.0,
                .less_than = 4.0,
                .step = 5.0
            }), sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_read_attrs(
            anjay, WRAPPED_OBJ2, 3, 1, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_read_attrs(
            anjay, WRAPPED_OBJ2, 3, 2, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_read_attrs(
            anjay, WRAPPED_OBJ2, 2, 2, 4, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &ANJAY_DM_ATTRIBS_EMPTY,
                                      sizeof(attrs));

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, write_resource_attrs) {
    DM_ATTR_STORAGE_TEST_INIT;
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));

    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 5, 3, &ANJAY_DM_ATTRIBS_EMPTY));
    // nothing actually changed
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_find_object(fas, 69)->instances);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 3, 1,
            &(const anjay_dm_attributes_t) {
                .min_period = 1,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = 34.0,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 69)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 69)->instances,
            test_instance_entry(
                    2,
                    NULL,
                    test_resource_entry(
                            3,
                            test_resource_attrs(
                                    1,
                                    1, ANJAY_ATTRIB_PERIOD_NONE,
                                    34.0, ANJAY_ATTRIB_VALUE_NONE, ANJAY_ATTRIB_VALUE_NONE),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 5, 3,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = 5,
                .greater_than = 6.0,
                .less_than = 7.0,
                .step = 8.0
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 3, 5,
            &(const anjay_dm_attributes_t) {
                .min_period = 9,
                .max_period = 10,
                .greater_than = 11.0,
                .less_than = 22.0,
                .step = 33.0
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 69)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 69)->instances,
            test_instance_entry(
                    2,
                    NULL,
                    test_resource_entry(
                            3,
                            test_resource_attrs(
                                    1,
                                    1, ANJAY_ATTRIB_PERIOD_NONE,
                                    34.0, ANJAY_ATTRIB_VALUE_NONE, ANJAY_ATTRIB_VALUE_NONE),
                            test_resource_attrs(5, 9, 10, 11.0, 22.0, 33.0),
                            NULL),
                    test_resource_entry(
                            5,
                            test_resource_attrs(3, 4, 5, 6.0, 7.0, 8.0),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ2, 2, 4,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 3, 5, &ANJAY_DM_ATTRIBS_EMPTY));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 3, 1, &ANJAY_DM_ATTRIBS_EMPTY));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 69)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 69)->instances,
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(4, 4, ANJAY_ATTRIB_PERIOD_NONE),
                            NULL),
                    test_resource_entry(
                            5,
                            test_resource_attrs(3, 4, 5, 6.0, 7.0, 8.0),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 5, 3, &ANJAY_DM_ATTRIBS_EMPTY));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 69)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 69)->instances,
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(4, 4, ANJAY_ATTRIB_PERIOD_NONE),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 3, 5,
            &(const anjay_dm_attributes_t) {
                .min_period = 9,
                .max_period = 10,
                .greater_than = 11.0,
                .less_than = 22.0,
                .step = 33.0
            }));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->instance_write_default_attrs(
            anjay, WRAPPED_OBJ2, 2, 4, &ANJAY_DM_ATTRIBS_EMPTY));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 69)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 69)->instances,
            test_instance_entry(
                    2,
                    NULL,
                    test_resource_entry(
                            3,
                            test_resource_attrs(5, 9, 10, 11.0, 22.0, 33.0),
                            NULL),
                    NULL));

    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ2)->resource_write_attrs(
            anjay, WRAPPED_OBJ2, 2, 3, 5, &ANJAY_DM_ATTRIBS_EMPTY));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_find_object(fas, 69)->instances);

    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
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

    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->default_attrs,
            test_default_attrs(2, 5, 6));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->default_attrs,
            test_default_attrs(4, 7, 8));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    1,
                    test_default_attrlist(
                            test_default_attrs(43, 101, 102),
                            test_default_attrs(515, 103, 104),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(4, 109, 110, -0.1, -0.2, -0.3),
                            NULL),
                    test_resource_entry(
                            2,
                            test_resource_attrs(8, 111, 112, -0.4, -0.5, -0.6),
                            test_resource_attrs(42, 113, 114, -0.7, -0.8, -0.9),
                            NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(42, 1, 2),
                            test_default_attrs(514, 3, 4),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(3, 9, 10, -1.0, -2.0, -3.0),
                            NULL),
                    test_resource_entry(
                            2,
                            test_resource_attrs(7, 11, 12, -4.0, -5.0, -6.0),
                            test_resource_attrs(42, 13, 14, -7.0, -8.0, -9.0),
                            NULL),
                    NULL));

    anjay_iid_t iid;
    void *cookie = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 514);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SECURITY)->instance_it(
            anjay, WRAPPED_SECURITY, &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 514);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 1, 0, 7);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SECURITY)->instance_it(
            anjay, WRAPPED_SECURITY, &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 7);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 2, 0, 42);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SECURITY)->instance_it(
            anjay, WRAPPED_SECURITY, &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 42);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 3, 0, 4);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SECURITY)->instance_it(
            anjay, WRAPPED_SECURITY, &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 4);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 4, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2, 10, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 4, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 4, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, 3));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2, 10, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 7, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 7, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, 2));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2, 10, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 42, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 42, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2, 10, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 514, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 514, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, -4));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SECURITY)->instance_it(
            anjay, WRAPPED_SECURITY, &iid, &cookie));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs), 1);
    assert_attrs_equal(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs,
            test_default_attrs(2, 5, 6));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(514, 3, 4),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(3, 9, 10, -1.0, -2.0, -3.0),
                            NULL),
                    NULL));

    cookie = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 11);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SERVER)->instance_it(
            anjay, WRAPPED_SERVER, &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 11);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 1, 0, 9);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SERVER)->instance_it(
            anjay, WRAPPED_SERVER, &iid, &cookie));
    AVS_UNIT_ASSERT_EQUAL(iid, 9);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 2, 0, 10);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SERVER)->instance_it(
            anjay, WRAPPED_SERVER, &iid, &cookie));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    AVS_UNIT_ASSERT_EQUAL(iid, 10);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 3, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 9, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 9, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 10, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 10, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, 2));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 11, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 11, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, -5));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SERVER)->instance_it(
            anjay, WRAPPED_SERVER, &iid, &cookie));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs), 1);
    assert_attrs_equal(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs,
            test_default_attrs(2, 5, 6));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(514, 3, 4),
                            NULL),
                    NULL));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, ssid_remove) {
    DM_ATTR_STORAGE_TEST_INIT;

    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->default_attrs,
            test_default_attrs(2, 5, 6));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->default_attrs,
            test_default_attrs(4, 7, 8));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    1,
                    test_default_attrlist(
                            test_default_attrs(43, 101, 102),
                            test_default_attrs(515, 103, 104),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(4, 109, 110, -0.1, -0.2, -0.3),
                            NULL),
                    test_resource_entry(
                            2,
                            test_resource_attrs(8, 111, 112, -0.4, -0.5, -0.6),
                            test_resource_attrs(42, 113, 114, -0.7, -0.8, -0.9),
                            NULL),
                    NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(42, 1, 2),
                            test_default_attrs(514, 3, 4),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(3, 9, 10, -1.0, -2.0, -3.0),
                            NULL),
                    test_resource_entry(
                            2,
                            test_resource_attrs(7, 11, 12, -4.0, -5.0, -6.0),
                            test_resource_attrs(42, 13, 14, -7.0, -8.0, -9.0),
                            NULL),
                    NULL));

    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2, 10, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 7, 10, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 7, 10, 0,
                                        ANJAY_MOCK_DM_INT(0, 2));
    _anjay_mock_dm_expect_instance_remove(anjay, &FAKE_SECURITY2, 7, 0);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SECURITY)->instance_remove(
            anjay, WRAPPED_SECURITY, 7));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs), 1);
    assert_attrs_equal(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs,
            test_default_attrs(4, 7, 8));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            2);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    1,
                    test_default_attrlist(
                            test_default_attrs(43, 101, 102),
                            test_default_attrs(515, 103, 104),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(4, 109, 110, -0.1, -0.2, -0.3),
                            NULL),
                    test_resource_entry(
                            2,
                            test_resource_attrs(8, 111, 112, -0.4, -0.5, -0.6),
                            test_resource_attrs(42, 113, 114, -0.7, -0.8, -0.9),
                            NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_find_object(fas, 42)->instances),
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(42, 1, 2),
                            test_default_attrs(514, 3, 4),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(3, 9, 10, -1.0, -2.0, -3.0),
                            NULL),
                    test_resource_entry(
                            2,
                            test_resource_attrs(7, 11, 12, -4.0, -5.0, -6.0),
                            test_resource_attrs(42, 13, 14, -7.0, -8.0, -9.0),
                            NULL),
                    NULL));

    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 19, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 19, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, 42));
    _anjay_mock_dm_expect_instance_remove(anjay, &FAKE_SERVER, 19, 0);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_SERVER)->instance_remove(
            anjay, WRAPPED_SERVER, 19));
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs), 1);
    assert_attrs_equal(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs,
            test_default_attrs(4, 7, 8));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            2);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    1,
                    test_default_attrlist(
                            test_default_attrs(43, 101, 102),
                            test_default_attrs(515, 103, 104),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(4, 109, 110, -0.1, -0.2, -0.3),
                            NULL),
                    test_resource_entry(
                            2,
                            test_resource_attrs(8, 111, 112, -0.4, -0.5, -0.6),
                            NULL),
                    NULL));
    assert_instance_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_find_object(fas, 42)->instances),
            test_instance_entry(
                    2,
                    test_default_attrlist(
                            test_default_attrs(514, 3, 4),
                            NULL),
                    test_resource_entry(
                            1,
                            test_resource_attrs(3, 9, 10, -1.0, -2.0, -3.0),
                            NULL),
                    test_resource_entry(
                            2,
                            test_resource_attrs(7, 11, 12, -4.0, -5.0, -6.0),
                            NULL),
                    NULL));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, nested_iterations) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_iid_t iid;

    // prepare initial state
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(1, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(2, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(3, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(4, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(5, NULL, NULL));

    void *cookie1 = NULL;
    void *cookie2 = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 1);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie1));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie1));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 1);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie2));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie2));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie2));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie2));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_TRUE(anjay_attr_storage_is_modified(fas));
    fas->modified_since_persist = false;
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie1));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie1));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            3);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(1, NULL, NULL));
    assert_instance_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_find_object(fas, 42)->instances),
            test_instance_entry(2, NULL, NULL));
    assert_instance_equal(
            AVS_LIST_NTH(_anjay_attr_storage_find_object(fas, 42)->instances,
                         2),
            test_instance_entry(3, NULL, NULL));
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));

    DM_ATTR_STORAGE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage, parallel_iterations) {
    DM_ATTR_STORAGE_TEST_INIT;
    anjay_iid_t iid;

    // prepare initial state
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(1, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(2, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(3, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(4, NULL, NULL));
    AVS_LIST_APPEND(&_anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(5, NULL, NULL));

    void *cookie1 = NULL;
    void *cookie2 = NULL;
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 1);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie1));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 1);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie2));
    AVS_UNIT_ASSERT_EQUAL(iid, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie1));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 2);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie2));
    AVS_UNIT_ASSERT_EQUAL(iid, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie1));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 3);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie2));
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie1));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS((*WRAPPED_OBJ)->instance_it(anjay, WRAPPED_OBJ,
                                                        &iid, &cookie2));
    AVS_UNIT_ASSERT_EQUAL(iid, ANJAY_IID_INVALID);

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            5);
    AVS_UNIT_ASSERT_FALSE(anjay_attr_storage_is_modified(fas));

    DM_ATTR_STORAGE_TEST_FINISH;
}
