/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL

#    include <inttypes.h>

#    include "anjay_mod_access_control.h"

VISIBILITY_SOURCE_BEGIN

//// HELPERS ///////////////////////////////////////////////////////////////////
access_control_t *_anjay_access_control_from_obj_ptr(obj_ptr_t obj_ptr) {
    return AVS_CONTAINER_OF(_anjay_dm_installed_object_get_unlocked(&obj_ptr),
                            access_control_t, obj_def);
}

static void
ac_instances_cleanup(AVS_LIST(access_control_instance_t) *instance_ptr) {
    AVS_LIST_CLEAR(instance_ptr) {
        AVS_LIST_CLEAR(&(*instance_ptr)->acl);
    }
}

void _anjay_access_control_clear_state(access_control_state_t *state) {
    ac_instances_cleanup(&state->instances);
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

static int
ac_commit_new_instance(anjay_unlocked_t *anjay,
                       access_control_t *ac,
                       AVS_LIST(access_control_instance_t) ac_instance) {
    int result = _anjay_notify_instances_changed_unlocked(
            anjay, ANJAY_DM_OID_ACCESS_CONTROL);
    if (result) {
        ac_log(ERROR,
               _("error while calling anjay_notify_instances_changed()"));
        return result;
    }
    anjay_notify_queue_t dm_changes = NULL;
    if (!(result = _anjay_access_control_add_instance(ac, ac_instance,
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
    return result;
}

static bool target_instance_reachable(anjay_unlocked_t *anjay,
                                      anjay_oid_t oid,
                                      anjay_iid_t iid) {
    if (!_anjay_access_control_target_oid_valid(oid)
            || !_anjay_access_control_target_iid_valid(iid)) {
        return false;
    }
    obj_ptr_t *target_obj = _anjay_dm_find_object_by_oid(anjay, oid);
    if (!target_obj) {
        return false;
    }
    return iid == ANJAY_ID_INVALID
           || _anjay_dm_instance_present(anjay, target_obj, iid) > 0;
}

AVS_LIST(access_control_instance_t)
_anjay_access_control_create_missing_ac_instance(const acl_target_t *target) {
    AVS_LIST(access_control_instance_t) aco_instance =
            AVS_LIST_NEW_ELEMENT(access_control_instance_t);
    if (!aco_instance) {
        return NULL;
    }

    *aco_instance = (access_control_instance_t) {
        .iid = ANJAY_ID_INVALID,
        .target = *target,
        .owner = ANJAY_SSID_BOOTSTRAP,
        .has_acl = true,
        .acl = NULL
    };
    return aco_instance;
}

static AVS_LIST(access_control_instance_t)
create_missing_ac_instance_with_validation(anjay_unlocked_t *anjay,
                                           anjay_oid_t oid,
                                           anjay_iid_t iid) {
    if (!target_instance_reachable(anjay, oid, iid)) {
        ac_log(WARNING,
               _("cannot set ACL: object instance ") "/%" PRIu16 "/%" PRIu16 _(
                       " does not exist"),
               oid, iid);
        return NULL;
    }
    AVS_LIST(access_control_instance_t) result =
            _anjay_access_control_create_missing_ac_instance(
                    &(const acl_target_t) { oid, iid });
    if (!result) {
        ac_log(WARNING,
               _("cannot set ACL: Access Control instance for ") "/%u/%u" _(
                       " does not exist and it could not be created"),
               oid, iid);
    }
    return result;
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

static int set_acl_in_instance(anjay_unlocked_t *anjay,
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

static int set_acl(anjay_unlocked_t *anjay,
                   access_control_t *ac,
                   anjay_oid_t oid,
                   anjay_iid_t iid,
                   anjay_ssid_t ssid,
                   anjay_access_mask_t access_mask) {
    bool ac_instance_needs_inserting = false;
    AVS_LIST(access_control_instance_t) ac_instance =
            find_ac_instance(ac, oid, iid);
    if (!ac_instance) {
        ac_instance =
                create_missing_ac_instance_with_validation(anjay, oid, iid);
        if (!ac_instance) {
            return -1;
        }
        ac_instance_needs_inserting = true;
    }

    int result = set_acl_in_instance(anjay, ac_instance, ssid, access_mask);
    if (!ac_instance_needs_inserting) {
        if (!result) {
            _anjay_access_control_mark_modified(ac);
            _anjay_notify_changed_unlocked(anjay, ANJAY_DM_OID_ACCESS_CONTROL,
                                           ac_instance->iid,
                                           ANJAY_DM_RID_ACCESS_CONTROL_ACL);
        }
        return result;
    }
    if (!result) {
        result = ac_commit_new_instance(anjay, ac, ac_instance);
    }
    if (result) {
        ac_instances_cleanup(&ac_instance);
    }
    return result;
}

int anjay_access_control_set_acl(anjay_t *anjay_locked,
                                 anjay_oid_t oid,
                                 anjay_iid_t iid,
                                 anjay_ssid_t ssid,
                                 anjay_access_mask_t access_mask) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    access_control_t *access_control = _anjay_access_control_get(anjay);
    if (!access_control) {
        ac_log(ERROR, _("Access Control not installed in this Anjay object"));
    } else if (ssid == ANJAY_SSID_BOOTSTRAP) {
        ac_log(ERROR,
               _("cannot set ACL: SSID = ") "%u" _(" is a reserved value"),
               ssid);
    } else if ((access_mask & ANJAY_ACCESS_MASK_FULL) != access_mask) {
        ac_log(ERROR, _("cannot set ACL: invalid permission mask"));
    } else if (iid != ANJAY_ID_INVALID
               && (access_mask & ANJAY_ACCESS_MASK_CREATE)) {
        ac_log(ERROR,
               _("cannot set ACL: Create permission makes no sense for "
                 "Object Instances"));
    } else if (iid == ANJAY_ID_INVALID
               && (access_mask & ANJAY_ACCESS_MASK_CREATE) != access_mask) {
        ac_log(ERROR,
               _("cannot set ACL: only Create permission makes sense for "
                 "creation instance"));
    } else {
        result = set_acl(anjay, access_control, oid, iid, ssid, access_mask);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static int ac_set_owner_unlocked(anjay_unlocked_t *anjay,
                                 access_control_t *ac,
                                 anjay_oid_t target_oid,
                                 anjay_iid_t target_iid,
                                 anjay_ssid_t owner_ssid,
                                 anjay_iid_t *inout_acl_iid) {
    if (owner_ssid == ANJAY_SSID_ANY) {
        ac_log(ERROR, _("Cannot set ACL owner: SSID = 0 is a reserved value"));
        return -1;
    }

    bool ac_instance_needs_inserting = false;
    AVS_LIST(access_control_instance_t) ac_instance =
            find_ac_instance(ac, target_oid, target_iid);
    if (ac_instance && inout_acl_iid && *inout_acl_iid != ANJAY_ID_INVALID
            && *inout_acl_iid != ac_instance->iid) {
        ac_log(ERROR,
               _("Cannot set ACL Instance ") "%" PRIu16 _(
                       ": conflicting instance ") "%" PRIu16,
               *inout_acl_iid, ac_instance->iid);
        *inout_acl_iid = ac_instance->iid;
        return -1;
    }

    if (!ac_instance) {
        ac_instance =
                create_missing_ac_instance_with_validation(anjay, target_oid,
                                                           target_iid);
        if (!ac_instance) {
            return -1;
        }
        ac_instance_needs_inserting = true;
        if (inout_acl_iid) {
            ac_instance->iid = *inout_acl_iid;
        }
    }
    if (owner_ssid != ac_instance->owner) {
        if (owner_ssid != ANJAY_SSID_BOOTSTRAP
                && _anjay_access_control_validate_ssid(anjay, owner_ssid)) {
            if (ac_instance_needs_inserting) {
                ac_instances_cleanup(&ac_instance);
            }
            ac_log(WARNING,
                   _("cannot set ACL owner: Server with SSID==") "%" PRIu16 _(
                           " does not exist"),
                   owner_ssid);
            return -1;
        }
        ac_instance->owner = owner_ssid;
    }
    int result = 0;
    if (!ac_instance_needs_inserting) {
        _anjay_access_control_mark_modified(ac);
        _anjay_notify_changed_unlocked(anjay, ANJAY_DM_OID_ACCESS_CONTROL,
                                       ac_instance->iid,
                                       ANJAY_DM_RID_ACCESS_CONTROL_OWNER);
    } else if ((result = ac_commit_new_instance(anjay, ac, ac_instance))) {
        ac_instances_cleanup(&ac_instance);
    }
    if (!result && inout_acl_iid) {
        *inout_acl_iid = ac_instance->iid;
    }
    return result;
}

int anjay_access_control_set_owner(anjay_t *anjay_locked,
                                   anjay_oid_t target_oid,
                                   anjay_iid_t target_iid,
                                   anjay_ssid_t owner_ssid,
                                   anjay_iid_t *inout_acl_iid) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    access_control_t *ac = _anjay_access_control_get(anjay);
    if (!ac) {
        ac_log(ERROR, _("Access Control not installed in this Anjay object"));
    } else {
        result = ac_set_owner_unlocked(anjay, ac, target_oid, target_iid,
                                       owner_ssid, inout_acl_iid);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

#    ifdef ANJAY_TEST
#        include "tests/modules/access_control/access_control.c"
#    endif // ANJAY_TEST

#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
