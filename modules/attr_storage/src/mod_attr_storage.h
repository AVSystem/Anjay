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

#ifndef ATTR_STORAGE_H
#define ATTR_STORAGE_H

#include <anjay/attr_storage.h>
#include <anjay/core.h>

#include <anjay_modules/utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define fas_log(...) _anjay_log(anjay_attr_storage, __VA_ARGS__)

typedef struct {
    anjay_ssid_t ssid;
    anjay_dm_internal_attrs_t attrs;
} fas_default_attrs_t;

typedef struct {
    anjay_ssid_t ssid;
    anjay_dm_internal_res_attrs_t attrs;
} fas_resource_attrs_t;

typedef struct {
    anjay_rid_t rid;
    AVS_LIST(fas_resource_attrs_t) attrs;
} fas_resource_entry_t;

typedef struct {
    anjay_iid_t iid;
    AVS_LIST(fas_default_attrs_t) default_attrs;
    AVS_LIST(fas_resource_entry_t) resources;
} fas_instance_entry_t;

typedef struct {
    anjay_oid_t oid;
    AVS_LIST(fas_default_attrs_t) default_attrs;
    AVS_LIST(fas_instance_entry_t) instances;
} fas_object_entry_t;

typedef struct {
    anjay_oid_t oid;
    AVS_LIST(anjay_iid_t) iids;
    void *last_cookie;
} fas_iteration_state_t;

typedef struct {
    size_t depth;
    avs_stream_abstract_t *persist_data;
    bool modified_since_persist;
} fas_saved_state_t;

typedef struct {
    AVS_LIST(fas_object_entry_t) objects;
    bool modified_since_persist;
    fas_iteration_state_t iteration;
    fas_saved_state_t saved_state;
} anjay_attr_storage_t;

extern const anjay_dm_module_t _anjay_attr_storage_MODULE;

void _anjay_attr_storage_clear(anjay_attr_storage_t *fas);

anjay_attr_storage_t *_anjay_attr_storage_get(anjay_t *anjay);

void _anjay_attr_storage_remove_instances_not_on_sorted_list(
        anjay_attr_storage_t *fas,
        fas_object_entry_t *object,
        AVS_LIST(anjay_iid_t) iids);

static inline void
_anjay_attr_storage_mark_modified(anjay_attr_storage_t *fas) {
    fas->modified_since_persist = true;
}

static void remove_resource_entry(anjay_attr_storage_t *fas,
                                  AVS_LIST(fas_resource_entry_t) *entry_ptr) {
    AVS_LIST_CLEAR(&(*entry_ptr)->attrs);
    AVS_LIST_DELETE(entry_ptr);
    _anjay_attr_storage_mark_modified(fas);
}

static void remove_instance_entry(anjay_attr_storage_t *fas,
                                  AVS_LIST(fas_instance_entry_t) *entry_ptr) {
    AVS_LIST_CLEAR(&(*entry_ptr)->default_attrs);
    while ((*entry_ptr)->resources) {
        remove_resource_entry(fas, &(*entry_ptr)->resources);
    }
    AVS_LIST_DELETE(entry_ptr);
    _anjay_attr_storage_mark_modified(fas);
}

static void remove_object_entry(anjay_attr_storage_t *fas,
                                AVS_LIST(fas_object_entry_t) *entry_ptr) {
    AVS_LIST_CLEAR(&(*entry_ptr)->default_attrs);
    while ((*entry_ptr)->instances) {
        remove_instance_entry(fas, &(*entry_ptr)->instances);
    }
    AVS_LIST_DELETE(entry_ptr);
    _anjay_attr_storage_mark_modified(fas);
}

static void
remove_instance_if_empty(AVS_LIST(fas_instance_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->default_attrs && !(*entry_ptr)->resources) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

static void remove_object_if_empty(AVS_LIST(fas_object_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->default_attrs && !(*entry_ptr)->instances) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

static inline anjay_ssid_t *get_ssid_ptr(void *generic_attrs) {
    AVS_STATIC_ASSERT(offsetof(fas_default_attrs_t, ssid) == 0,
                      default_attrs_ssid_offset);
    AVS_STATIC_ASSERT(offsetof(fas_resource_attrs_t, ssid) == 0,
                      resource_attrs_ssid_offset);
    return (anjay_ssid_t *) generic_attrs;
}

static inline void *get_attrs_ptr(void *generic_attrs,
                                  size_t attrs_field_offset) {
    return (char *) generic_attrs + attrs_field_offset;
}

typedef bool is_empty_func_t(const void *attrs);

static bool default_attrs_empty(const void *attrs) {
    return _anjay_dm_attributes_empty(
            (const anjay_dm_internal_attrs_t *) attrs);
}

static bool resource_attrs_empty(const void *attrs) {
    return _anjay_dm_resource_attributes_empty(
            (const anjay_dm_internal_res_attrs_t *) attrs);
}

int _anjay_attr_storage_compare_u16ids(const void *a,
                                       const void *b,
                                       size_t element_size);

int _anjay_attr_storage_persist_inner(anjay_attr_storage_t *attr_storage,
                                      avs_stream_abstract_t *out);

int _anjay_attr_storage_restore_inner(anjay_t *anjay,
                                      anjay_attr_storage_t *attr_storage,
                                      avs_stream_abstract_t *in);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ATTR_STORAGE_H */
