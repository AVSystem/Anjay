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

#ifndef ATTR_STORAGE_TEST_H
#define ATTR_STORAGE_TEST_H

#include <avsystem/commons/list.h>
#include <avsystem/commons/unit/test.h>

#include "../attr_storage.h"

static fas_attrs_t *test_resource_attrs(anjay_ssid_t ssid,
                                        time_t min_period,
                                        time_t max_period,
                                        double greater_than,
                                        double less_than,
                                        double step) {
    fas_attrs_t *attrs = AVS_LIST_NEW_ELEMENT(fas_attrs_t);
    AVS_UNIT_ASSERT_NOT_NULL(attrs);
    attrs->ssid = ssid;
    attrs->attrs.min_period = min_period;
    attrs->attrs.max_period = max_period;
    attrs->attrs.greater_than = greater_than;
    attrs->attrs.less_than = less_than;
    attrs->attrs.step = step;
    return attrs;
}

/* Using anjay_rid_t instead of int / unsigned in va_start is UB */
static fas_resource_entry_t *test_resource_entry(unsigned /*anjay_rid_t*/ rid, ...) {
    assert(rid <= UINT16_MAX);
    fas_resource_entry_t *resource = AVS_LIST_NEW_ELEMENT(fas_resource_entry_t);
    AVS_UNIT_ASSERT_NOT_NULL(resource);
    resource->rid = (anjay_rid_t) rid;
    va_list ap;
    va_start(ap, rid);
    fas_attrs_t *attrs;
    while ((attrs = va_arg(ap, fas_attrs_t *))) {
        AVS_LIST_APPEND(&resource->attrs, attrs);
    }
    va_end(ap);
    return resource;
}

static fas_attrs_t *test_default_attrs(anjay_ssid_t ssid,
                                       time_t min_period,
                                       time_t max_period) {
    fas_attrs_t *attrs = AVS_LIST_NEW_ELEMENT(fas_attrs_t);
    AVS_UNIT_ASSERT_NOT_NULL(attrs);
    attrs->ssid = ssid;
    attrs->attrs.min_period = min_period;
    attrs->attrs.max_period = max_period;
    attrs->attrs.greater_than = ANJAY_ATTRIB_VALUE_NONE;
    attrs->attrs.less_than = ANJAY_ATTRIB_VALUE_NONE;
    attrs->attrs.step = ANJAY_ATTRIB_VALUE_NONE;
    return attrs;
}

static AVS_LIST(fas_attrs_t)
test_default_attrlist(fas_attrs_t *entry, ...) {
    AVS_LIST(fas_attrs_t) attrlist = NULL;
    va_list ap;
    va_start(ap, entry);
    for (; entry; entry = va_arg(ap, fas_attrs_t *)) {
        AVS_LIST_APPEND(&attrlist, entry);
    }
    va_end(ap);
    return attrlist;
}

static fas_instance_entry_t *
test_instance_entry(anjay_iid_t iid,
                    AVS_LIST(fas_attrs_t) default_attrs,
                    ...) {
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

static void assert_attrs_equal(fas_attrs_t *actual, fas_attrs_t *tmp_expected) {
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(actual, tmp_expected, sizeof(*actual));
    AVS_LIST_DELETE(&tmp_expected);
}

static void assert_resource_equal(fas_resource_entry_t *actual,
                                  fas_resource_entry_t *tmp_expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->rid, tmp_expected->rid);

    size_t count = AVS_LIST_SIZE(tmp_expected->attrs);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(actual->attrs), count);
    AVS_LIST(fas_attrs_t) attrs = actual->attrs;
    while (count--) {
        assert_attrs_equal(attrs, AVS_LIST_DETACH(&tmp_expected->attrs));
        attrs = AVS_LIST_NEXT(attrs);
    }

    AVS_LIST_DELETE(&tmp_expected);
}

static void assert_instance_equal(fas_instance_entry_t *actual,
                                  fas_instance_entry_t *tmp_expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->iid, tmp_expected->iid);

    size_t count = AVS_LIST_SIZE(tmp_expected->default_attrs);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(actual->default_attrs), count);
    AVS_LIST(fas_attrs_t) default_attrs = actual->default_attrs;
    while (count--) {
        assert_attrs_equal(
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

#endif /* ATTR_STORAGE_TEST_H */

