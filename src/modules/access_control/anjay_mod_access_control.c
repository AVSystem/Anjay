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

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL

#    include <inttypes.h>

#    include "anjay_mod_access_control.h"

VISIBILITY_SOURCE_BEGIN

//// HELPERS ///////////////////////////////////////////////////////////////////
access_control_t *_anjay_access_control_from_obj_ptr(obj_ptr_t obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return AVS_CONTAINER_OF(obj_ptr, access_control_t, obj_def);
}

void _anjay_access_control_clear_state(access_control_state_t *state) {
    AVS_LIST_CLEAR(&state->instances) {
        AVS_LIST_CLEAR(&state->instances->acl);
    }
    state->modified_since_persist = false;
}

int _anjay_access_control_clone_state(access_control_state_t *dest,
                                      const access_control_state_t *src) {
    assert(!dest->instances);
    AVS_LIST(access_control_instance_t) *dest_tail = &dest->instances;
    AVS_LIST(access_control_instance_t) src_inst;
    AVS_LIST_FOREACH(src_inst, src->instances) {
        AVS_LIST(access_control_instance_t) dest_inst =
                AVS_LIST_NEW_ELEMENT(access_control_instance_t);
        if (!dest_inst) {
            goto error;
        }
        AVS_LIST_INSERT(dest_tail, dest_inst);
        AVS_LIST_ADVANCE_PTR(&dest_tail);
        *dest_inst = *src_inst;
        dest_inst->acl = NULL;
        AVS_LIST(acl_entry_t) *dest_acl_tail = &dest_inst->acl;
        AVS_LIST(acl_entry_t) src_acl;
        AVS_LIST_FOREACH(src_acl, src_inst->acl) {
            AVS_LIST(acl_entry_t) dest_acl = AVS_LIST_NEW_ELEMENT(acl_entry_t);
            if (!dest_acl) {
                goto error;
            }
            AVS_LIST_INSERT(dest_acl_tail, dest_acl);
            AVS_LIST_ADVANCE_PTR(&dest_acl_tail);
            *dest_acl = *src_acl;
        }
    }
    dest->modified_since_persist = src->modified_since_persist;
    return 0;
error:
    _anjay_access_control_clear_state(dest);
    return -1;
}

static int add_instances_without_iids(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t) *instances_to_move,
        anjay_notify_queue_t *out_dm_changes) {
    AVS_LIST(access_control_instance_t) *insert_ptr =
            &access_control->current.instances;
    anjay_iid_t proposed_iid = 0;
    while (*instances_to_move && proposed_iid < ANJAY_ID_INVALID) {
        assert((*instances_to_move)->iid == ANJAY_ID_INVALID);
        if (!*insert_ptr || proposed_iid < (*insert_ptr)->iid) {
            if (out_dm_changes) {
                int result = _anjay_notify_queue_instance_created(
                        out_dm_changes, ANJAY_DM_OID_ACCESS_CONTROL,
                        proposed_iid);
                if (result) {
                    return result;
                }
            }
            AVS_LIST_INSERT(insert_ptr, AVS_LIST_DETACH(instances_to_move));
            (*insert_ptr)->iid = proposed_iid;
        }
        // proposed_iid cannot possibly be GREATER than (*insert_ptr)->iid
        assert(proposed_iid == (*insert_ptr)->iid);
        ++proposed_iid;
        AVS_LIST_ADVANCE_PTR(&insert_ptr);
    }

    if (*instances_to_move) {
        ac_log(ERROR, _("no free IIDs left"));
        return -1;
    }
    return 0;
}

int _anjay_access_control_add_instance(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t) instance,
        anjay_notify_queue_t *out_dm_changes) {
    assert(!AVS_LIST_NEXT(instance));
    if (instance->iid == ANJAY_ID_INVALID) {
        return add_instances_without_iids(access_control, &instance,
                                          out_dm_changes);
    }

    AVS_LIST(access_control_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &access_control->current.instances) {
        if ((*ptr)->iid == instance->iid) {
            ac_log(WARNING,
                   _("element with IID == ") "%" PRIu16 _(" already exists"),
                   instance->iid);
            return -1;
        } else if ((*ptr)->iid > instance->iid) {
            break;
        }
    }
    int result = 0;
    if (out_dm_changes) {
        result = _anjay_notify_queue_instance_created(
                out_dm_changes, ANJAY_DM_OID_ACCESS_CONTROL, instance->iid);
    }
    if (!result) {
        AVS_LIST_INSERT(ptr, instance);
    }
    return result;
}

AVS_LIST(access_control_instance_t)
_anjay_access_control_create_missing_ac_instance(anjay_ssid_t owner,
                                                 const acl_target_t *target) {
    AVS_LIST(access_control_instance_t) aco_instance =
            AVS_LIST_NEW_ELEMENT(access_control_instance_t);
    AVS_LIST(acl_entry_t) acl_entry = NULL;
    if (!aco_instance) {
        goto cleanup;
    }

    if (owner != ANJAY_SSID_BOOTSTRAP && target->iid != ANJAY_ID_INVALID) {
        if (!(acl_entry = AVS_LIST_NEW_ELEMENT(acl_entry_t))) {
            goto cleanup;
        }
        acl_entry->mask = (ANJAY_ACCESS_MASK_FULL & ~ANJAY_ACCESS_MASK_CREATE);
        acl_entry->ssid = owner;
    }
    *aco_instance = (access_control_instance_t) {
        .iid = ANJAY_ID_INVALID,
        .target = *target,
        .owner = owner,
        .has_acl = true,
        .acl = acl_entry
    };
    return aco_instance;
cleanup:
    AVS_LIST_CLEAR(&aco_instance);
    AVS_LIST_CLEAR(&acl_entry);
    return NULL;
}

static access_control_instance_t *
find_ac_instance(access_control_t *ac, anjay_oid_t oid, anjay_iid_t iid) {
    AVS_LIST(access_control_instance_t) it;
    AVS_LIST_FOREACH(it, ac->current.instances) {
        if (it->target.oid == oid && it->target.iid == iid) {
            return it;
        }
    }

    return NULL;
}

static bool
target_instance_reachable(anjay_t *anjay, anjay_oid_t oid, anjay_iid_t iid) {
    if (!_anjay_access_control_target_oid_valid(oid)
            || !_anjay_access_control_target_iid_valid(iid)) {
        return false;
    }
    obj_ptr_t target_obj = _anjay_dm_find_object_by_oid(anjay, oid);
    if (!target_obj) {
        return false;
    }
    return iid == ANJAY_ID_INVALID
           || _anjay_dm_instance_present(anjay, target_obj, iid) > 0;
}

static int set_acl_in_instance(anjay_t *anjay,
                               access_control_instance_t *ac_instance,
                               anjay_ssid_t ssid,
                               anjay_access_mask_t access_mask) {
    AVS_LIST(acl_entry_t) *insert_ptr;
    AVS_LIST_FOREACH_PTR(insert_ptr, &ac_instance->acl) {
        if ((*insert_ptr)->ssid >= ssid) {
            break;
        }
    }

    if (!*insert_ptr || (*insert_ptr)->ssid != ssid) {
        if (_anjay_access_control_validate_ssid(anjay, ssid)) {
            ac_log(WARNING,
                   _("cannot set ACL: Server with SSID==") "%" PRIu16 _(
                           " does not exist"),
                   ssid);
            return -1;
        }

        AVS_LIST(acl_entry_t) new_entry = AVS_LIST_NEW_ELEMENT(acl_entry_t);
        if (!new_entry) {
            ac_log(ERROR, _("out of memory"));
            return -1;
        }

        AVS_LIST_INSERT(insert_ptr, new_entry);
        ac_instance->has_acl = true;
        new_entry->ssid = ssid;
    }

    (*insert_ptr)->mask = access_mask;
    return 0;
}

static int set_acl(anjay_t *anjay,
                   access_control_t *ac,
                   anjay_oid_t oid,
                   anjay_iid_t iid,
                   anjay_ssid_t ssid,
                   anjay_access_mask_t access_mask) {
    bool ac_instance_needs_inserting = false;
    AVS_LIST(access_control_instance_t) ac_instance =
            find_ac_instance(ac, oid, iid);
    if (!ac_instance) {
        if (!target_instance_reachable(anjay, oid, iid)) {
            ac_log(WARNING,
                   _("cannot set ACL: object instance ") "/%" PRIu16 "/%" PRIu16
                           _(" does not exist"),
                   oid, iid);
            return -1;
        }
        ac_instance = _anjay_access_control_create_missing_ac_instance(
                ANJAY_SSID_BOOTSTRAP, &(const acl_target_t) { oid, iid });
        if (!ac_instance) {
            ac_log(WARNING,
                   _("cannot set ACL: Access Control instance for ") "/%u/%u" _(
                           " does not exist and it could not be created"),
                   oid, iid);
            return -1;
        }
        ac_instance_needs_inserting = true;
    }

    int result = set_acl_in_instance(anjay, ac_instance, ssid, access_mask);
    if (!ac_instance_needs_inserting) {
        if (!result) {
            _anjay_access_control_mark_modified(ac);
        }
        return result;
    }

    if (!result
            && (result = anjay_notify_instances_changed(
                        anjay, ANJAY_DM_OID_ACCESS_CONTROL))) {
        ac_log(ERROR,
               _("error while calling anjay_notify_instances_changed()"));
    }
    anjay_notify_queue_t dm_changes = NULL;
    if (!result
            && !(result = _anjay_access_control_add_instance(ac, ac_instance,
                                                             &dm_changes))) {
        assert(AVS_LIST_SIZE(dm_changes) == 1);
        assert(AVS_LIST_SIZE(dm_changes->instance_set_changes.known_added_iids)
               == 1);
        assert(!dm_changes->resources_changed);
        _anjay_access_control_mark_modified(ac);
        _anjay_notify_instance_created(
                anjay, dm_changes->oid,
                *dm_changes->instance_set_changes.known_added_iids);
        _anjay_notify_clear_queue(&dm_changes);
    }
    if (result) {
        AVS_LIST_CLEAR(&ac_instance) {
            AVS_LIST_CLEAR(&ac_instance->acl);
        }
    }
    return result;
}

int anjay_access_control_set_acl(anjay_t *anjay,
                                 anjay_oid_t oid,
                                 anjay_iid_t iid,
                                 anjay_ssid_t ssid,
                                 anjay_access_mask_t access_mask) {
    access_control_t *access_control = _anjay_access_control_get(anjay);
    if (!access_control) {
        ac_log(ERROR, _("Access Control not installed in this Anjay object"));
        return -1;
    }

    if (ssid == ANJAY_SSID_BOOTSTRAP) {
        ac_log(ERROR,
               _("cannot set ACL: SSID = ") "%u" _(" is a reserved value"),
               ssid);
        return -1;
    }
    if ((access_mask & ANJAY_ACCESS_MASK_FULL) != access_mask) {
        ac_log(ERROR, _("cannot set ACL: invalid permission mask"));
        return -1;
    }
    if (iid != ANJAY_ID_INVALID && (access_mask & ANJAY_ACCESS_MASK_CREATE)) {
        ac_log(ERROR, _("cannot set ACL: Create permission makes no sense for "
                        "Object Instances"));
        return -1;
    }
    if (iid == ANJAY_ID_INVALID
            && (access_mask & ANJAY_ACCESS_MASK_CREATE) != access_mask) {
        ac_log(ERROR,
               _("cannot set ACL: only Create permission makes sense for "
                 "creation instance"));
        return -1;
    }

    return set_acl(anjay, access_control, oid, iid, ssid, access_mask);
}

#    ifdef ANJAY_TEST
#        include "tests/modules/access_control/access_control.c"
#    endif // ANJAY_TEST

#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
