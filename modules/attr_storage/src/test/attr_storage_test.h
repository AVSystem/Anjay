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

#ifndef ATTR_STORAGE_TEST_H
#define ATTR_STORAGE_TEST_H

#include <avsystem/commons/list.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/utils.h>

#include "../mod_attr_storage.h"

static fas_resource_attrs_t *test_resource_attrs(anjay_ssid_t ssid,
                                                 int32_t min_period,
                                                 int32_t max_period,
                                                 double greater_than,
                                                 double less_than,
                                                 double step,
                                                 anjay_dm_con_attr_t con) {
    fas_resource_attrs_t *attrs = AVS_LIST_NEW_ELEMENT(fas_resource_attrs_t);
    AVS_UNIT_ASSERT_NOT_NULL(attrs);
    attrs->ssid = ssid;
    attrs->attrs = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;
    attrs->attrs.standard.common.min_period = min_period;
    attrs->attrs.standard.common.max_period = max_period;
    attrs->attrs.standard.greater_than = greater_than;
    attrs->attrs.standard.less_than = less_than;
    attrs->attrs.standard.step = step;
#ifdef WITH_CUSTOM_ATTRIBUTES
    attrs->attrs.custom.data.con = con;
#endif // WITH_CUSTOM_ATTRIBUTES
    return attrs;
}

/* Using anjay_rid_t instead of int / unsigned in va_start is UB */
static fas_resource_entry_t *test_resource_entry(unsigned /*anjay_rid_t*/ rid,
                                                 ...) {
    assert(rid <= UINT16_MAX);
    fas_resource_entry_t *resource = AVS_LIST_NEW_ELEMENT(fas_resource_entry_t);
    AVS_UNIT_ASSERT_NOT_NULL(resource);
    resource->rid = (anjay_rid_t) rid;
    va_list ap;
    va_start(ap, rid);
    fas_resource_attrs_t *attrs;
    while ((attrs = va_arg(ap, fas_resource_attrs_t *))) {
        AVS_LIST_APPEND(&resource->attrs, attrs);
    }
    va_end(ap);
    return resource;
}

static fas_default_attrs_t *test_default_attrs(anjay_ssid_t ssid,
                                               int32_t min_period,
                                               int32_t max_period,
                                               anjay_dm_con_attr_t con) {
    fas_default_attrs_t *attrs = AVS_LIST_NEW_ELEMENT(fas_default_attrs_t);
    AVS_UNIT_ASSERT_NOT_NULL(attrs);
    attrs->ssid = ssid;
    attrs->attrs = ANJAY_DM_INTERNAL_ATTRS_EMPTY;
    attrs->attrs.standard.min_period = min_period;
    attrs->attrs.standard.max_period = max_period;
#ifdef WITH_CUSTOM_ATTRIBUTES
    attrs->attrs.custom.data.con = con;
#endif // WITH_CUSTOM_ATTRIBUTES
    return attrs;
}

static AVS_LIST(fas_default_attrs_t)
test_default_attrlist(fas_default_attrs_t *entry, ...) {
    AVS_LIST(fas_default_attrs_t) attrlist = NULL;
    va_list ap;
    va_start(ap, entry);
    for (; entry; entry = va_arg(ap, fas_default_attrs_t *)) {
        AVS_LIST_APPEND(&attrlist, entry);
    }
    va_end(ap);
    return attrlist;
}

static fas_instance_entry_t *test_instance_entry(
        anjay_iid_t iid, AVS_LIST(fas_default_attrs_t) default_attrs, ...) {
    fas_instance_entry_t *instance = AVS_LIST_NEW_ELEMENT(fas_instance_entry_t);
    AVS_UNIT_ASSERT_NOT_NULL(instance);
    instance->iid = iid;
    instance->default_attrs = default_attrs;
    va_list ap;
    va_start(ap, default_attrs);
    fas_resource_entry_t *resource;
    while ((resource = va_arg(ap, fas_resource_entry_t *))) {
        AVS_LIST_APPEND(&instance->resources, resource);
    }
    va_end(ap);
    return instance;
}

static fas_object_entry_t *test_object_entry(
        anjay_oid_t oid, AVS_LIST(fas_default_attrs_t) default_attrs, ...) {
    fas_object_entry_t *object = AVS_LIST_NEW_ELEMENT(fas_object_entry_t);
    AVS_UNIT_ASSERT_NOT_NULL(object);
    object->oid = oid;
    object->default_attrs = default_attrs;
    va_list ap;
    va_start(ap, default_attrs);
    fas_instance_entry_t *instance;
    while ((instance = va_arg(ap, fas_instance_entry_t *))) {
        AVS_LIST_APPEND(&object->instances, instance);
    }
    va_end(ap);
    return object;
}

static void assert_attrs_equal(const anjay_dm_internal_attrs_t *actual,
                               const anjay_dm_internal_attrs_t *expected) {
#ifdef WITH_CUSTOM_ATTRIBUTES
    AVS_UNIT_ASSERT_EQUAL(actual->custom.data.con, expected->custom.data.con);
#endif // WITH_CUSTOM_ATTRIBUTES
    AVS_UNIT_ASSERT_EQUAL(actual->standard.min_period,
                          expected->standard.min_period);
    AVS_UNIT_ASSERT_EQUAL(actual->standard.max_period,
                          expected->standard.max_period);
}

static void
assert_res_attrs_equal(const anjay_dm_internal_res_attrs_t *actual,
                       const anjay_dm_internal_res_attrs_t *expected) {
    assert_attrs_equal(
            _anjay_dm_get_internal_attrs_const(&actual->standard.common),
            _anjay_dm_get_internal_attrs_const(&expected->standard.common));
    AVS_UNIT_ASSERT_EQUAL(actual->standard.greater_than,
                          expected->standard.greater_than);
    AVS_UNIT_ASSERT_EQUAL(actual->standard.less_than,
                          expected->standard.less_than);
    AVS_UNIT_ASSERT_EQUAL(actual->standard.step, expected->standard.step);
}

static void assert_fas_default_attrs_equal(fas_default_attrs_t *actual,
                                           fas_default_attrs_t *tmp_expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->ssid, tmp_expected->ssid);
    assert_attrs_equal(&actual->attrs, &tmp_expected->attrs);
    AVS_LIST_DELETE(&tmp_expected);
}

static void
assert_fas_resource_attrs_equal(fas_resource_attrs_t *actual,
                                fas_resource_attrs_t *tmp_expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->ssid, tmp_expected->ssid);
    assert_res_attrs_equal(&actual->attrs, &tmp_expected->attrs);
    AVS_LIST_DELETE(&tmp_expected);
}

static void assert_resource_equal(fas_resource_entry_t *actual,
                                  fas_resource_entry_t *tmp_expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->rid, tmp_expected->rid);

    size_t count = AVS_LIST_SIZE(tmp_expected->attrs);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(actual->attrs), count);
    AVS_LIST(fas_resource_attrs_t) attrs = actual->attrs;
    while (count--) {
        assert_fas_resource_attrs_equal(attrs,
                                        AVS_LIST_DETACH(&tmp_expected->attrs));
        attrs = AVS_LIST_NEXT(attrs);
    }

    AVS_LIST_DELETE(&tmp_expected);
}

static void assert_instance_equal(fas_instance_entry_t *actual,
                                  fas_instance_entry_t *tmp_expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->iid, tmp_expected->iid);

    size_t count = AVS_LIST_SIZE(tmp_expected->default_attrs);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(actual->default_attrs), count);
    AVS_LIST(fas_default_attrs_t) default_attrs = actual->default_attrs;
    while (count--) {
        assert_fas_default_attrs_equal(
                default_attrs, AVS_LIST_DETACH(&tmp_expected->default_attrs));
        default_attrs = AVS_LIST_NEXT(default_attrs);
    }

    count = AVS_LIST_SIZE(tmp_expected->resources);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(actual->resources), count);
    AVS_LIST(fas_resource_entry_t) resource = actual->resources;
    while (count--) {
        assert_resource_equal(resource,
                              AVS_LIST_DETACH(&tmp_expected->resources));
        resource = AVS_LIST_NEXT(resource);
    }

    AVS_LIST_DELETE(&tmp_expected);
}

static void assert_object_equal(fas_object_entry_t *actual,
                                fas_object_entry_t *tmp_expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->oid, tmp_expected->oid);
    size_t count = AVS_LIST_SIZE(tmp_expected->default_attrs);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(actual->default_attrs), count);
    AVS_LIST(fas_default_attrs_t) default_attrs = actual->default_attrs;
    while (count--) {
        assert_fas_default_attrs_equal(
                default_attrs, AVS_LIST_DETACH(&tmp_expected->default_attrs));
        default_attrs = AVS_LIST_NEXT(default_attrs);
    }

    count = AVS_LIST_SIZE(tmp_expected->instances);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(actual->instances), count);
    AVS_LIST(fas_instance_entry_t) instance = actual->instances;
    while (count--) {
        assert_instance_equal(instance,
                              AVS_LIST_DETACH(&tmp_expected->instances));
        instance = AVS_LIST_NEXT(instance);
    }

    AVS_LIST_DELETE(&tmp_expected);
}

#endif /* ATTR_STORAGE_TEST_H */
