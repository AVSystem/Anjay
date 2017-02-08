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

#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H

#include <assert.h>

#include <avsystem/commons/stream/stream_membuf.h>

#include <anjay/access_control.h>

#include <anjay_modules/dm.h>
#include <anjay_modules/notify.h>
#include <anjay_modules/utils.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ac_log(...) _anjay_log(access_control, __VA_ARGS__)

typedef struct access_control_struct access_control_t;

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
} access_control_state_t;

struct access_control_struct {
    const anjay_dm_object_def_t *obj_def;
    anjay_t *anjay;
    access_control_state_t current;
    access_control_state_t saved_state;
    bool needs_validation;
    bool sync_in_progress;
};

typedef const anjay_dm_object_def_t *const *obj_ptr_t;

access_control_t *
_anjay_access_control_get(const anjay_dm_object_def_t *const *obj_ptr);

void _anjay_access_control_clear_state(access_control_state_t *state);

int _anjay_access_control_clone_state(access_control_state_t *dest,
                                      const access_control_state_t *src);

int _anjay_access_control_sync_instances(access_control_t *access_control,
                                         anjay_ssid_t origin_ssid,
                                         AVS_LIST(anjay_oid_t) oids_to_sync,
                                         anjay_notify_queue_t *out_dm_changes);

int _anjay_access_control_remove_orphaned_instances(
        access_control_t *access_control, anjay_notify_queue_t *out_dm_changes);

int _anjay_access_control_validate_ssid(anjay_t *anjay, anjay_ssid_t ssid);

int _anjay_access_control_add_instances_without_iids(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t) *instances_to_move);

int _anjay_access_control_add_instance(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t) instance);

static inline bool _anjay_access_control_target_oid_valid(int32_t oid) {
    return oid >= 1 && oid != ANJAY_DM_OID_ACCESS_CONTROL && oid < UINT16_MAX;
}

static inline bool _anjay_access_control_target_iid_valid(int32_t iid) {
    // checks whether iid is within valid range for anjay_iid_t; otherwise
    // (canonically, iid == -1), it would mean that it is not present
    return iid == (anjay_iid_t) iid;
}
VISIBILITY_PRIVATE_HEADER_END

#endif /* ACCESS_CONTROL_H */
