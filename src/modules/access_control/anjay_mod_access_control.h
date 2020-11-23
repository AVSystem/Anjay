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

#ifndef MOD_ACCESS_CONTROL_H
#define MOD_ACCESS_CONTROL_H

#include <assert.h>

#include <avsystem/commons/avs_stream_membuf.h>

#include <anjay/access_control.h>

#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_notify.h>
#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ac_log(...) _anjay_log(access_control, __VA_ARGS__)

typedef struct {
    anjay_access_mask_t mask;
    anjay_ssid_t ssid;
} acl_entry_t;

typedef struct {
    anjay_oid_t oid;
    int32_t iid; // negative means "not set yet"; must be set before commit
} acl_target_t;

typedef struct {
    anjay_iid_t iid;
    acl_target_t target;
    anjay_ssid_t owner;
    bool has_acl;
    AVS_LIST(acl_entry_t) acl;
} access_control_instance_t;

typedef struct {
    AVS_LIST(access_control_instance_t) instances;
    bool modified_since_persist;
} access_control_state_t;

typedef struct {
    const anjay_dm_object_def_t *obj_def;
    access_control_state_t current;
    access_control_state_t saved_state;
    bool in_transaction;
    access_control_instance_t *last_accessed_instance;
    bool needs_validation;
    bool sync_in_progress;
} access_control_t;

static inline void _anjay_access_control_mark_modified(access_control_t *repr) {
    repr->current.modified_since_persist = true;
}

static inline void
_anjay_access_control_clear_modified(access_control_t *repr) {
    repr->current.modified_since_persist = false;
}

typedef const anjay_dm_object_def_t *const *obj_ptr_t;

access_control_t *
_anjay_access_control_from_obj_ptr(const anjay_dm_object_def_t *const *obj_ptr);

access_control_t *_anjay_access_control_get(anjay_t *anjay);

void _anjay_access_control_clear_state(access_control_state_t *state);

int _anjay_access_control_clone_state(access_control_state_t *dest,
                                      const access_control_state_t *src);

int _anjay_access_control_validate_ssid(anjay_t *anjay, anjay_ssid_t ssid);

int _anjay_access_control_add_instance(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t) instance,
        anjay_notify_queue_t *out_dm_changes);

AVS_LIST(access_control_instance_t)
_anjay_access_control_create_missing_ac_instance(anjay_ssid_t owner,
                                                 const acl_target_t *target);

static inline bool _anjay_access_control_target_oid_valid(int32_t oid) {
    return oid >= 1 && oid != ANJAY_DM_OID_ACCESS_CONTROL && oid < UINT16_MAX;
}

static inline bool _anjay_access_control_target_iid_valid(int32_t iid) {
    // checks whether iid is within valid range for anjay_iid_t; otherwise
    // (canonically, iid == -1), it would mean that it is not present
    return iid == (anjay_iid_t) iid;
}
VISIBILITY_PRIVATE_HEADER_END

#endif /* MOD_ACCESS_CONTROL_H */
