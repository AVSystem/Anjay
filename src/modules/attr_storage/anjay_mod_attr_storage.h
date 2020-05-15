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

#ifndef ATTR_STORAGE_H
#define ATTR_STORAGE_H

#include <anjay/attr_storage.h>
#include <anjay/core.h>

#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define as_log(...) _anjay_log(anjay_attr_storage, __VA_ARGS__)

typedef struct {
    anjay_ssid_t ssid;
    anjay_dm_internal_oi_attrs_t attrs;
} as_default_attrs_t;

typedef struct {
    anjay_ssid_t ssid;
    anjay_dm_internal_r_attrs_t attrs;
} as_resource_attrs_t;

typedef struct {
    anjay_riid_t riid;
    AVS_LIST(as_resource_attrs_t) attrs;
} as_resource_instance_entry_t;

typedef struct {
    anjay_rid_t rid;
    AVS_LIST(as_resource_attrs_t) attrs;
} as_resource_entry_t;

typedef struct {
    anjay_iid_t iid;
    AVS_LIST(as_default_attrs_t) default_attrs;
    AVS_LIST(as_resource_entry_t) resources;
} as_instance_entry_t;

typedef struct {
    anjay_oid_t oid;
    AVS_LIST(as_default_attrs_t) default_attrs;
    AVS_LIST(as_instance_entry_t) instances;
} as_object_entry_t;

typedef struct {
    size_t depth;
    avs_stream_t *persist_data;
    bool modified_since_persist;
} as_saved_state_t;

typedef struct {
    AVS_LIST(as_object_entry_t) objects;
    bool modified_since_persist;
    as_saved_state_t saved_state;
} anjay_attr_storage_t;

extern const anjay_dm_module_t _anjay_attr_storage_MODULE;

void _anjay_attr_storage_clear(anjay_attr_storage_t *as);

anjay_attr_storage_t *_anjay_attr_storage_get(anjay_t *anjay);

/**
 * @param instance_ptr_ptr_
 * Conceptually of type AVS_LIST(as_instance_entry_t) **. Pointer to a variable
 * that is used to iterate over a list of instances. Before the first call in
 * iretarion, it shall point always points to the list's head.
 */
int _anjay_attr_storage_remove_absent_instances_clb(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *def_ptr,
        anjay_iid_t iid,
        void *instance_ptr_ptr_);

typedef struct {
    anjay_rid_t rid;
    anjay_dm_resource_kind_t kind;
    anjay_dm_resource_presence_t presence;
} resource_entry_t;

int _anjay_attr_storage_remove_absent_resources(
        anjay_t *anjay,
        anjay_attr_storage_t *as,
        AVS_LIST(as_instance_entry_t) *instance_ptr,
        const anjay_dm_object_def_t *const *def_ptr);

static inline void _anjay_attr_storage_mark_modified(anjay_attr_storage_t *as) {
    as->modified_since_persist = true;
}

static void remove_resource_entry(anjay_attr_storage_t *as,
                                  AVS_LIST(as_resource_entry_t) *entry_ptr) {
    AVS_LIST_CLEAR(&(*entry_ptr)->attrs);
    AVS_LIST_DELETE(entry_ptr);
    _anjay_attr_storage_mark_modified(as);
}

static void remove_instance_entry(anjay_attr_storage_t *as,
                                  AVS_LIST(as_instance_entry_t) *entry_ptr) {
    AVS_LIST_CLEAR(&(*entry_ptr)->default_attrs);
    while ((*entry_ptr)->resources) {
        remove_resource_entry(as, &(*entry_ptr)->resources);
    }
    AVS_LIST_DELETE(entry_ptr);
    _anjay_attr_storage_mark_modified(as);
}

static void remove_object_entry(anjay_attr_storage_t *as,
                                AVS_LIST(as_object_entry_t) *entry_ptr) {
    AVS_LIST_CLEAR(&(*entry_ptr)->default_attrs);
    while ((*entry_ptr)->instances) {
        remove_instance_entry(as, &(*entry_ptr)->instances);
    }
    AVS_LIST_DELETE(entry_ptr);
    _anjay_attr_storage_mark_modified(as);
}

static void remove_object_if_empty(AVS_LIST(as_object_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->default_attrs && !(*entry_ptr)->instances) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

static inline anjay_ssid_t *get_ssid_ptr(void *generic_attrs) {
    AVS_STATIC_ASSERT(offsetof(as_default_attrs_t, ssid) == 0,
                      default_attrs_ssid_offset);
    AVS_STATIC_ASSERT(offsetof(as_resource_attrs_t, ssid) == 0,
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
            (const anjay_dm_internal_oi_attrs_t *) attrs);
}

static bool resource_attrs_empty(const void *attrs) {
    return _anjay_dm_resource_attributes_empty(
            (const anjay_dm_internal_r_attrs_t *) attrs);
}

avs_error_t
_anjay_attr_storage_persist_inner(anjay_attr_storage_t *attr_storage,
                                  avs_stream_t *out);

avs_error_t _anjay_attr_storage_restore_inner(
        anjay_t *anjay, anjay_attr_storage_t *attr_storage, avs_stream_t *in);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ATTR_STORAGE_H */
