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

#ifndef ATTR_STORAGE_H
#define ATTR_STORAGE_H

#include <anjay/attr_storage.h>
#include <anjay/anjay.h>

#include <anjay_modules/utils.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define fas_log(...) _anjay_log(anjay_attr_storage, __VA_ARGS__)

typedef struct {
    anjay_ssid_t ssid;
    anjay_dm_attributes_t attrs;
} fas_attrs_t;

typedef struct {
    anjay_rid_t rid;
    AVS_LIST(fas_attrs_t) attrs;
} fas_resource_entry_t;

typedef struct {
    anjay_iid_t iid;
    AVS_LIST(fas_attrs_t) default_attrs;
    AVS_LIST(fas_resource_entry_t) resources;
} fas_instance_entry_t;

typedef struct {
    const anjay_dm_object_def_t *def_ptr;
    const anjay_dm_object_def_t *const *backend;
    anjay_attr_storage_t *fas;
    anjay_dm_object_def_t def;
    AVS_LIST(anjay_iid_t) instance_it_iids;
    void *instance_it_last_cookie;
    AVS_LIST(fas_attrs_t) default_attrs;
    AVS_LIST(fas_instance_entry_t) instances;
} fas_object_t;

typedef struct {
    avs_stream_abstract_t *persist_data;
    bool modified_since_persist;
} fas_saved_state_t;

struct anjay_attr_storage_struct {
    anjay_t *anjay;
    AVS_LIST(fas_object_t) objects;
    bool modified_since_persist;
    fas_saved_state_t saved_state;
};

fas_object_t *_anjay_attr_storage_find_object(anjay_attr_storage_t *fas,
                                              anjay_oid_t oid);

void _anjay_attr_storage_clear_object(fas_object_t *obj);

static inline void mark_modified(anjay_attr_storage_t *fas) {
    fas->modified_since_persist = true;
}

static void remove_resource_entry(anjay_attr_storage_t *fas,
                                  AVS_LIST(fas_resource_entry_t) *entry_ptr) {
    AVS_LIST_CLEAR(&(*entry_ptr)->attrs);
    AVS_LIST_DELETE(entry_ptr);
    mark_modified(fas);
}

static void
remove_instance_if_empty(AVS_LIST(fas_instance_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->default_attrs && !(*entry_ptr)->resources) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

int _anjay_attr_storage_persist_inner(anjay_attr_storage_t *attr_storage,
                                      avs_stream_abstract_t *out);

int _anjay_attr_storage_restore_inner(anjay_attr_storage_t *attr_storage,
                                      avs_stream_abstract_t *in);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ATTR_STORAGE_H */
