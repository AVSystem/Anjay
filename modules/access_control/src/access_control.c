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

#include <config.h>

#include <inttypes.h>

#include <anjay_modules/observe.h>

#include "access_control.h"

VISIBILITY_SOURCE_BEGIN

//// HELPERS ///////////////////////////////////////////////////////////////////
access_control_t *
_anjay_access_control_get(obj_ptr_t obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return AVS_CONTAINER_OF(obj_ptr, access_control_t, obj_def);
}

void _anjay_access_control_clear_state(access_control_state_t *state) {
    AVS_LIST_CLEAR(&state->instances) {
        AVS_LIST_CLEAR(&state->instances->acl);
    }
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
        dest_tail = AVS_LIST_NEXT_PTR(dest_tail);
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
            dest_acl_tail = AVS_LIST_NEXT_PTR(dest_acl_tail);
            *dest_acl = *src_acl;
        }
    }
    return 0;
error:
    _anjay_access_control_clear_state(dest);
    return -1;
}

static bool
has_instance_multiple_owners(AVS_LIST(access_control_instance_t) it) {
    AVS_LIST(acl_entry_t) entry;
    AVS_LIST_FOREACH(entry, it->acl) {
        if (entry->ssid != it->owner && entry->mask != ANJAY_ACCESS_MASK_NONE) {
            return true;
        }
    }
    return false;
}

static int
remove_referred_instance(anjay_t *anjay,
                         AVS_LIST(access_control_instance_t) it) {
    // We do not fail if either of the following is true:
    // - the target Object does not exist
    // - the target Instance is not set
    // - the target Instance does not exist
    int result = 0;
    obj_ptr_t obj = _anjay_dm_find_object_by_oid(anjay, it->target.oid);
    if (obj
            && _anjay_access_control_target_iid_valid(it->target.iid)
            && _anjay_dm_instance_present(anjay, obj,
                                          (anjay_iid_t) it->target.iid) > 0) {
        result = _anjay_dm_instance_remove(anjay, obj,
                                           (anjay_iid_t) it->target.iid);
    }
    if (result) {
        ac_log(ERROR,
               "cannot remove assigned Object Instance /%" PRIu16 "/%" PRId32,
               it->target.oid, it->target.iid);
    }
    return result;
}

static anjay_ssid_t elect_instance_owner(AVS_LIST(acl_entry_t) acl) {
    static const size_t write_weight = 1;
    static const size_t delete_weight = 1;

    /* Clearly we cannot perform election otherwise. */
    assert(AVS_LIST_SIZE(acl) > 0);

    anjay_ssid_t new_owner = ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP;
    size_t highest_sum = 0;

    AVS_LIST(acl_entry_t) entry;
    AVS_LIST_FOREACH(entry, acl) {
        size_t sum = (entry->mask & ANJAY_ACCESS_MASK_WRITE) * write_weight +
                     (entry->mask & ANJAY_ACCESS_MASK_DELETE) * delete_weight;
        if (sum >= highest_sum) {
            highest_sum = sum;
            new_owner = entry->ssid;
        }
    }
    return new_owner;
}

int _anjay_access_control_add_instances_without_iids(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t) *instances_to_move) {
    AVS_LIST(access_control_instance_t) *insert_ptr =
            &access_control->current.instances;
    anjay_iid_t proposed_iid = 0;
    while (*instances_to_move && proposed_iid < ANJAY_IID_INVALID) {
        assert((*instances_to_move)->iid == ANJAY_IID_INVALID);
        if (!*insert_ptr || proposed_iid < (*insert_ptr)->iid) {
            AVS_LIST_INSERT(insert_ptr, AVS_LIST_DETACH(instances_to_move));
            (*insert_ptr)->iid = proposed_iid;
        }
        // proposed_iid cannot possibly be GREATER than (*insert_ptr)->iid
        assert(proposed_iid == (*insert_ptr)->iid);
        ++proposed_iid;
        insert_ptr = AVS_LIST_NEXT_PTR(insert_ptr);
    }

    if (*instances_to_move) {
        ac_log(ERROR, "no free IIDs left");
        return -1;
    }
    return 0;
}

int _anjay_access_control_add_instance(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t) instance) {
    assert(!AVS_LIST_NEXT(instance));
    if (instance->iid == ANJAY_IID_INVALID) {
        return _anjay_access_control_add_instances_without_iids(access_control,
                                                                &instance);
    }

    AVS_LIST(access_control_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &access_control->current.instances) {
        if ((*ptr)->iid == instance->iid) {
            ac_log(ERROR, "element with IID == %" PRIu16 " already exists",
                   instance->iid);
            return -1;
        } else if ((*ptr)->iid > instance->iid) {
            break;
        }
    }
    AVS_LIST_INSERT(ptr, instance);
    return 0;
}

static int add_target_list_entry(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_iid_t iid,
                                 void *list_ptr_) {
    (void) anjay;
    AVS_LIST(acl_target_t) *list_ptr = (AVS_LIST(acl_target_t) *) list_ptr_;
    AVS_LIST(acl_target_t) new_element =
            AVS_LIST_INSERT_NEW(acl_target_t, list_ptr);
    if (!new_element) {
        ac_log(ERROR, "Out of memory");
        return -1;
    }
    new_element->oid = (*obj)->oid;
    new_element->iid = iid;
    return 0;
}

static int acl_target_compare(const acl_target_t *left,
                              const acl_target_t *right) {
    if (left->oid != right->oid) {
        return left->oid - right->oid;
    } else {
        return left->iid - right->iid;
    }
}

static int acl_target_compare_for_sort(const void *left,
                                       const void *right,
                                       size_t element_size) {
    (void) element_size;
    assert(element_size == sizeof(acl_target_t));
    return acl_target_compare((const acl_target_t *) left,
                              (const acl_target_t *) right);
}

static int enumerate_ac_targets_present_in_object(
        anjay_t *anjay, AVS_LIST(acl_target_t) *out_targets, anjay_oid_t oid) {
    assert(!*out_targets);

    obj_ptr_t obj = _anjay_dm_find_object_by_oid(anjay, oid);
    if (!obj) {
        // for our purposes, no object may not be an error
        // we just expect no ACL objects referring to it
        return 0;
    }

    // first, the Access Control instance that controls instance creation
    AVS_LIST(acl_target_t) create_instance =
            AVS_LIST_INSERT_NEW(acl_target_t, out_targets);
    if (!create_instance) {
        ac_log(ERROR, "Out of memory");
        return -1;
    }
    create_instance->oid = (*obj)->oid;
    create_instance->iid = UINT16_MAX;

    // next, the Access Control instances mirroring existing instances
    int result = _anjay_dm_foreach_instance(anjay, obj,
                                            add_target_list_entry, out_targets);
    if (result) {
        return result;
    }

    AVS_LIST_SORT(out_targets, acl_target_compare_for_sort);
    return 0;
}

static int enumerate_ac_targets_present_in_dm(
        anjay_t *anjay,
        AVS_LIST(acl_target_t) *out_targets,
        AVS_LIST(anjay_oid_t) oids_to_sync) {
    int result;
    AVS_LIST(acl_target_t) *tail_ptr = out_targets;
    AVS_LIST(anjay_oid_t) it;
    AVS_LIST_FOREACH(it, oids_to_sync) {
        if ((result = enumerate_ac_targets_present_in_object(anjay, tail_ptr,
                                                             *it))) {
            return result;
        }
        tail_ptr = AVS_LIST_APPEND_PTR(tail_ptr);
    }
    return 0;
}

static bool oid_present_on_list(AVS_LIST(anjay_oid_t) oids, anjay_oid_t oid) {
    AVS_LIST_ITERATE(oids) {
        if (*oids == oid) {
            return true;
        }
    }
    return false;
}

static int acl_instance_ptr_compare_by_target_for_sort(const void *left_,
                                                       const void *right_,
                                                       size_t element_size) {
    (void) element_size;
    assert(element_size == sizeof(access_control_instance_t const * const *));
    const acl_target_t *left =
            &(*(access_control_instance_t const * const *) left_)->target;
    const acl_target_t *right =
            &(*(access_control_instance_t const * const *) right_)->target;
    return acl_target_compare(left, right);
}

static int
enumerate_present_ac_instances(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t const *) *out_acls,
        AVS_LIST(anjay_oid_t) target_oids) {
    AVS_LIST(access_control_instance_t) acl;
    AVS_LIST_FOREACH(acl, access_control->current.instances) {
        if (!oid_present_on_list(target_oids, acl->target.oid)) {
            continue;
        }
        AVS_LIST(access_control_instance_t const *) new_entry =
                AVS_LIST_INSERT_NEW(access_control_instance_t const *,
                                    out_acls);
        if (!new_entry) {
            ac_log(ERROR, "Out of memory");
            return -1;
        }
        *new_entry = acl;
    }
    AVS_LIST_SORT(out_acls, acl_instance_ptr_compare_by_target_for_sort);
    return 0;
}

static AVS_LIST(access_control_instance_t)
create_missing_ac_instance(anjay_ssid_t owner,
                           const acl_target_t *target) {
    AVS_LIST(access_control_instance_t) aco_instance =
            AVS_LIST_NEW_ELEMENT(access_control_instance_t);
    AVS_LIST(acl_entry_t) acl_entry = NULL;
    if (!aco_instance) {
        goto cleanup;
    }

    if (owner != ANJAY_SSID_BOOTSTRAP && target->iid != UINT16_MAX) {
        if (!(acl_entry = AVS_LIST_NEW_ELEMENT(acl_entry_t))) {
            goto cleanup;
        }
        acl_entry->mask = (ANJAY_ACCESS_MASK_FULL & ~ANJAY_ACCESS_MASK_CREATE);
        acl_entry->ssid = owner;
    }
    *aco_instance = (access_control_instance_t) {
        .iid = ANJAY_IID_INVALID,
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

static int acl_instance_ptr_compare_by_iid(const void *left_,
                                           const void *right_,
                                           size_t element_size) {
    (void) element_size;
    assert(element_size == sizeof(access_control_instance_t const * const *));
    anjay_iid_t left =
            (*(access_control_instance_t const * const *) left_)->iid;
    anjay_iid_t right =
            (*(access_control_instance_t const * const *) right_)->iid;
    return left - right;
}

static void remove_ac_instances_orphaned_after_sync(
        access_control_t *access_control,
        AVS_LIST(access_control_instance_t const *) *to_remove) {
    AVS_LIST_SORT(to_remove, acl_instance_ptr_compare_by_iid);
    AVS_LIST(access_control_instance_t) *instance_ptr;
    AVS_LIST(access_control_instance_t) instance_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(instance_ptr, instance_helper,
                                   &access_control->current.instances) {
        if (!*to_remove) {
            return;
        } else if (*instance_ptr == **to_remove) {
            AVS_LIST_CLEAR(&(*instance_ptr)->acl);
            AVS_LIST_DELETE(instance_ptr);
            AVS_LIST_DELETE(to_remove);
        }
    }
}

typedef enum {
    SYNC_REMOVE_AC_INSTANCE,
    SYNC_ADD_MISSING_AC_INSTANCE,
    SYNC_CONTINUE
} sync_action_t;

static sync_action_t
determine_sync_action(AVS_LIST(acl_target_t) present_dm_instances,
                      AVS_LIST(access_control_instance_t const*) existing_acs) {
    if (present_dm_instances && !existing_acs) {
        // some required ACs don't exist, create missing
        return SYNC_ADD_MISSING_AC_INSTANCE;
    } else if (!present_dm_instances && existing_acs) {
        // excessive ACs exist, remove
        return SYNC_REMOVE_AC_INSTANCE;
    } else {
        assert(present_dm_instances && existing_acs);
        int diff = acl_target_compare(present_dm_instances,
                                      &(*existing_acs)->target);
        if (diff < 0) {
            // (*existing_acs)->target not present in data model
            return SYNC_ADD_MISSING_AC_INSTANCE;
        } else if (diff > 0) {
            // AC instance does not exist for (*existing_acs)->target
            return SYNC_REMOVE_AC_INSTANCE;
        } else {
            assert(diff == 0);
            return SYNC_CONTINUE;
        }
    }
}

int _anjay_access_control_sync_instances(access_control_t *access_control,
                                         anjay_ssid_t origin_ssid,
                                         AVS_LIST(anjay_oid_t) oids_to_sync,
                                         anjay_notify_queue_t *out_dm_changes) {
    obj_ptr_t possibly_wrapped_ac = _anjay_dm_find_object_by_oid(
            access_control->anjay, ANJAY_DM_OID_ACCESS_CONTROL);
    assert(possibly_wrapped_ac);
    int result;
    AVS_LIST(acl_target_t) present_dm_instances = NULL;
    AVS_LIST(access_control_instance_t const *) existing_acs = NULL;
    AVS_LIST(access_control_instance_t const *) acs_to_remove = NULL;
    AVS_LIST(access_control_instance_t) acs_to_insert = NULL;
    AVS_LIST(access_control_instance_t) *acs_to_insert_append_ptr
            = &acs_to_insert;

    (void) ((result = enumerate_ac_targets_present_in_dm(
                    access_control->anjay, &present_dm_instances, oids_to_sync))
            || (result = enumerate_present_ac_instances(
                    access_control, &existing_acs, oids_to_sync)));

    while (!result && (present_dm_instances || existing_acs)) {
        switch (determine_sync_action(present_dm_instances, existing_acs)) {
        case SYNC_REMOVE_AC_INSTANCE:
            // mark the orphaned Access Control instance for removal
            if (!(result = _anjay_dm_transaction_include_object(
                    access_control->anjay, possibly_wrapped_ac))) {
                AVS_LIST_INSERT(&acs_to_remove, AVS_LIST_DETACH(&existing_acs));
            }
            break;

        case SYNC_ADD_MISSING_AC_INSTANCE:
            result = _anjay_dm_transaction_include_object(access_control->anjay,
                                                          possibly_wrapped_ac);
            if (!result) {
                AVS_LIST(access_control_instance_t) new_instance =
                        create_missing_ac_instance(origin_ssid,
                                                   present_dm_instances);
                if (!AVS_LIST_INSERT(acs_to_insert_append_ptr, new_instance)) {
                    result = -1;
                }
            }
            AVS_LIST_DELETE(&present_dm_instances);
            acs_to_insert_append_ptr =
                    AVS_LIST_APPEND_PTR(acs_to_insert_append_ptr);
            break;

        case SYNC_CONTINUE:
            AVS_LIST_DELETE(&present_dm_instances);
            AVS_LIST_DELETE(&existing_acs);
            break;
        }
    }
    AVS_LIST_CLEAR(&present_dm_instances);
    AVS_LIST_CLEAR(&existing_acs);
    if (acs_to_remove) {
        remove_ac_instances_orphaned_after_sync(access_control, &acs_to_remove);
        if (!result) {
            result = _anjay_notify_queue_instance_set_unknown_change(
                    out_dm_changes, ANJAY_DM_OID_ACCESS_CONTROL);
        }
    }
    assert(!acs_to_remove);
    if (acs_to_insert) {
        if (!result) {
            result = _anjay_access_control_add_instances_without_iids(
                    access_control, &acs_to_insert);
        }
        if (!result) {
            result = _anjay_notify_queue_instance_set_unknown_change(
                out_dm_changes, ANJAY_DM_OID_ACCESS_CONTROL);
        }
        AVS_LIST_CLEAR(&acs_to_insert) {
            AVS_LIST_CLEAR(&acs_to_insert->acl);
        }
    }
    return result;
}

int _anjay_access_control_remove_orphaned_instances(
        access_control_t *access_control,
        anjay_notify_queue_t *out_dm_changes) {
    obj_ptr_t possibly_wrapped_ac = _anjay_dm_find_object_by_oid(
            access_control->anjay, ANJAY_DM_OID_ACCESS_CONTROL);
    assert(possibly_wrapped_ac);
    AVS_LIST(access_control_instance_t) *curr;
    AVS_LIST(access_control_instance_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(curr, helper,
                                   &access_control->current.instances) {
        if ((*curr)->owner == ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP
                || !_anjay_access_control_validate_ssid(access_control->anjay,
                                                        (*curr)->owner)) {
            continue;
        }
        int result = _anjay_dm_transaction_include_object(access_control->anjay,
                                                          possibly_wrapped_ac);
        if (result) {
            return result;
        }
        if (!has_instance_multiple_owners(*curr)) {
            /* Try to remove referred Instance according to Appendix E.1.3 */
            if ((result = remove_referred_instance(access_control->anjay,
                                                   *curr))
                    || (result = _anjay_notify_queue_instance_removed(
                            out_dm_changes, ANJAY_DM_OID_ACCESS_CONTROL,
                            (*curr)->iid))) {
                return result;
            }
            AVS_LIST_CLEAR(&(*curr)->acl);
            AVS_LIST_DELETE(curr);
        } else {
            AVS_LIST(acl_entry_t) *entry;
            AVS_LIST_FOREACH_PTR(entry, &(*curr)->acl) {
                if ((*entry)->ssid == (*curr)->owner) {
                    AVS_LIST_DELETE(entry);
                    break;
                }
            }
            (*curr)->owner = elect_instance_owner((*curr)->acl);
            if ((result = _anjay_notify_queue_resource_change(
                            out_dm_changes, ANJAY_DM_OID_ACCESS_CONTROL,
                            (*curr)->iid, ANJAY_DM_RID_ACCESS_CONTROL_ACL))
                    || (result = _anjay_notify_queue_resource_change(
                            out_dm_changes, ANJAY_DM_OID_ACCESS_CONTROL,
                            (*curr)->iid, ANJAY_DM_RID_ACCESS_CONTROL_OWNER))) {
                return result;
            }
        }
    }
    return 0;
}

void anjay_access_control_object_delete(obj_ptr_t obj) {
    access_control_t *access_control = _anjay_access_control_get(obj);
    if (access_control) {
        _anjay_access_control_clear_state(&access_control->current);
        _anjay_access_control_clear_state(&access_control->saved_state);
        free(access_control);
    }
}

static access_control_instance_t *find_ac_instance(access_control_t *ac,
                                                   anjay_oid_t oid,
                                                   anjay_iid_t iid) {
    AVS_LIST(access_control_instance_t) it;
    AVS_LIST_FOREACH(it, ac->current.instances) {
        if (it->target.oid == oid && it->target.iid == iid) {
            return it;
        }
    }

    return NULL;
}

static bool target_instance_reachable(anjay_t *anjay,
                                      anjay_oid_t oid,
                                      anjay_iid_t iid) {
    if (!_anjay_access_control_target_oid_valid(oid)
            || !_anjay_access_control_target_iid_valid(iid)) {
        return false;
    }
    obj_ptr_t target_obj = _anjay_dm_find_object_by_oid(anjay, oid);
    if (!target_obj) {
        return false;
    }
    return iid == UINT16_MAX
            || _anjay_dm_instance_present(anjay, target_obj, iid) > 0;
}

static int set_acl_in_instance(anjay_t *anjay,
                               access_control_instance_t *ac_instance,
                               anjay_ssid_t ssid,
                               anjay_access_mask_t access_mask) {
    AVS_LIST(acl_entry_t) entry;
    AVS_LIST_FOREACH(entry, ac_instance->acl) {
        if (entry->ssid == ssid) {
            break;
        }
    }

    if (!entry) {
        if (_anjay_access_control_validate_ssid(anjay, ssid)) {
            ac_log(ERROR, "cannot set ACL: Server with SSID==%" PRIu16 " does "
                   "not exist", ssid);
            return -1;
        }

        entry = AVS_LIST_NEW_ELEMENT(acl_entry_t);
        if (!entry) {
            ac_log(ERROR, "out of memory");
            return -1;
        }

        AVS_LIST_INSERT(&ac_instance->acl, entry);
        ac_instance->has_acl = true;
        entry->ssid = ssid;
    }

    entry->mask = access_mask;
    return 0;
}

static int set_acl(access_control_t *ac,
                   anjay_oid_t oid,
                   anjay_iid_t iid,
                   anjay_ssid_t ssid,
                   anjay_access_mask_t access_mask) {
    bool ac_instance_needs_inserting = false;
    AVS_LIST(access_control_instance_t) ac_instance =
            find_ac_instance(ac, oid, iid);
    if (!ac_instance) {
        if (!target_instance_reachable(ac->anjay, oid, iid)) {
            ac_log(ERROR, "cannot set ACL: object instance "
                   "/%" PRIu16 "/%" PRIu16 " does not exist", oid, iid);
            return -1;
        }
        ac_instance = create_missing_ac_instance(
                ANJAY_SSID_BOOTSTRAP, &(const acl_target_t) { oid, iid });
        if (!ac_instance) {
            ac_log(ERROR, "cannot set ACL: Access Control instance for /%u/%u "
                   "does not exist and it could not be created", oid, iid);
            return -1;
        }
        ac_instance_needs_inserting = true;
    }

    int result = set_acl_in_instance(ac->anjay, ac_instance, ssid, access_mask);

    if (ac_instance_needs_inserting) {
        if (!result
                && (result = anjay_notify_instances_changed(
                        ac->anjay, ANJAY_DM_OID_ACCESS_CONTROL))) {
            ac_log(ERROR,
                   "error while calling anjay_notify_instances_changed()");
        }
        if (!result
                && (result = _anjay_access_control_add_instance(ac,
                                                                ac_instance))) {
            AVS_LIST_CLEAR(&ac_instance) {
                AVS_LIST_CLEAR(&ac_instance->acl);
            }
        }
    }
    return result;
}

int anjay_access_control_set_acl(const anjay_dm_object_def_t *const *ac_obj_ptr,
                                 anjay_oid_t oid,
                                 anjay_iid_t iid,
                                 anjay_ssid_t ssid,
                                 anjay_access_mask_t access_mask) {
    if (ssid == UINT16_MAX) {
        ac_log(ERROR, "cannot set ACL: SSID = %u is a reserved value", ssid);
        return -1;
    }
    if ((access_mask & ANJAY_ACCESS_MASK_FULL) != access_mask) {
        ac_log(ERROR, "cannot set ACL: invalid permission mask");
        return -1;
    }
    if (iid != UINT16_MAX && (access_mask & ANJAY_ACCESS_MASK_CREATE)) {
        ac_log(ERROR, "cannot set ACL: Create permission makes no sense for "
               "Object Instances");
        return -1;
    }
    if (iid == UINT16_MAX
            && (access_mask & ANJAY_ACCESS_MASK_CREATE) != access_mask) {
        ac_log(ERROR, "cannot set ACL: only Create permission makes sense for "
               "creation instance");
        return -1;
    }

    access_control_t *access_control = _anjay_access_control_get(ac_obj_ptr);
    if (!access_control) {
        ac_log(ERROR, "invalid Access Control object pointer");
        return -1;
    }

    return set_acl(access_control, oid, iid, ssid, access_mask);
}

#ifdef ANJAY_TEST
#include "test/access_control.c"
#endif // ANJAY_TEST
