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

#include <anjay_config.h>

#include <inttypes.h>
#include <string.h>

#include <avsystem/commons/rbtree.h>

#include <anjay_modules/notify.h>

#include "mod_access_control.h"

VISIBILITY_SOURCE_BEGIN

static access_control_instance_t *
find_instance(access_control_t *access_control, anjay_iid_t iid) {
    if (access_control) {
        access_control_instance_t *it;
        AVS_LIST_FOREACH(it, access_control->current.instances) {
            if (it->iid == iid) {
                return it;
            } else if (it->iid > iid) {
                break;
            }
        }
    }
    return NULL;
}

static int ac_instance_it(anjay_t *anjay,
                          obj_ptr_t obj_ptr,
                          anjay_iid_t *out,
                          void **cookie) {
    (void) anjay;
    access_control_t *access_control =
            _anjay_access_control_from_obj_ptr(obj_ptr);
    if (!access_control) {
        return ANJAY_ERR_INTERNAL;
    }
    AVS_LIST(access_control_instance_t) curr =
            (AVS_LIST(access_control_instance_t)) *cookie;

    if (!curr) {
        curr = access_control->current.instances;
    } else {
        curr = AVS_LIST_NEXT(curr);
    }
    *out = (anjay_iid_t) (curr ? curr->iid : ANJAY_IID_INVALID);
    *cookie = curr;

    return 0;
}

static int
ac_instance_present(anjay_t *anjay, obj_ptr_t obj_ptr, anjay_iid_t iid) {
    (void) anjay;
    return find_instance(_anjay_access_control_from_obj_ptr(obj_ptr), iid)
           != NULL;
}

static int
ac_instance_reset(anjay_t *anjay, obj_ptr_t obj_ptr, anjay_iid_t iid) {
    (void) anjay;
    access_control_t *access_control =
            _anjay_access_control_from_obj_ptr(obj_ptr);
    access_control_instance_t *inst = find_instance(access_control, iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }
    AVS_LIST_CLEAR(&inst->acl);
    inst->has_acl = false;
    inst->owner = 0;
    access_control->needs_validation = true;
    _anjay_access_control_mark_modified(access_control);
    return 0;
}

static int ac_instance_create(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t *inout_iid,
                              anjay_ssid_t ssid) {
    (void) anjay;
    access_control_t *access_control =
            _anjay_access_control_from_obj_ptr(obj_ptr);
    AVS_LIST(access_control_instance_t) new_instance =
            AVS_LIST_NEW_ELEMENT(access_control_instance_t);
    if (!new_instance) {
        ac_log(ERROR, "Out of memory");
        return ANJAY_ERR_INTERNAL;
    }
    *new_instance = (access_control_instance_t) {
        .iid = *inout_iid,
        .target = {
            .oid = 0,
            .iid = -1
        },
        .owner = ssid,
        .has_acl = false,
        .acl = NULL
    };
    int retval = _anjay_access_control_add_instance(access_control,
                                                    new_instance, NULL);
    if (retval) {
        AVS_LIST_CLEAR(&new_instance);
    }
    access_control->needs_validation = true;
    _anjay_access_control_mark_modified(access_control);
    return retval;
}

static int ac_instance_remove(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid) {
    (void) anjay;
    access_control_t *access_control =
            _anjay_access_control_from_obj_ptr(obj_ptr);
    AVS_LIST(access_control_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &access_control->current.instances) {
        if ((*it)->iid == iid) {
            AVS_LIST_CLEAR(&(*it)->acl);
            AVS_LIST_DELETE(it);
            _anjay_access_control_mark_modified(access_control);
            return 0;
        } else if ((*it)->iid > iid) {
            break;
        }
    }
    return ANJAY_ERR_NOT_FOUND;
}

static int ac_resource_present(anjay_t *anjay,
                               obj_ptr_t obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid) {
    (void) anjay;
    switch (rid) {
    case ANJAY_DM_RID_ACCESS_CONTROL_OID:
    case ANJAY_DM_RID_ACCESS_CONTROL_OIID:
    case ANJAY_DM_RID_ACCESS_CONTROL_OWNER:
        return 1;
    case ANJAY_DM_RID_ACCESS_CONTROL_ACL: {
        access_control_instance_t *inst =
                find_instance(_anjay_access_control_from_obj_ptr(obj_ptr), iid);
        if (inst) {
            return inst->has_acl ? 1 : 0;
        } else {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    default:
        return 0;
    }
}

static int ac_resource_operations(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_rid_t rid,
                                  anjay_dm_resource_op_mask_t *out) {
    (void) anjay;
    (void) obj_ptr;
    *out = ANJAY_DM_RESOURCE_OP_NONE;
    switch (rid) {
    case ANJAY_DM_RID_ACCESS_CONTROL_OID:
    case ANJAY_DM_RID_ACCESS_CONTROL_OIID:
        *out = ANJAY_DM_RESOURCE_OP_BIT_R;
        break;
    case ANJAY_DM_RID_ACCESS_CONTROL_ACL:
    case ANJAY_DM_RID_ACCESS_CONTROL_OWNER:
        *out = ANJAY_DM_RESOURCE_OP_BIT_R | ANJAY_DM_RESOURCE_OP_BIT_W;
        break;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
    return 0;
}

static int ac_resource_read(anjay_t *anjay,
                            obj_ptr_t obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    access_control_instance_t *inst =
            find_instance(_anjay_access_control_from_obj_ptr(obj_ptr), iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case ANJAY_DM_RID_ACCESS_CONTROL_OID:
        return anjay_ret_i32(ctx, (int32_t) inst->target.oid);
    case ANJAY_DM_RID_ACCESS_CONTROL_OIID:
        return anjay_ret_i32(ctx, (int32_t) inst->target.iid);
    case ANJAY_DM_RID_ACCESS_CONTROL_ACL: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array) {
            return ANJAY_ERR_INTERNAL;
        }
        acl_entry_t *it;
        AVS_LIST_FOREACH(it, inst->acl) {
            if (anjay_ret_array_index(array, it->ssid)
                    || anjay_ret_i32(array, it->mask)) {
                return ANJAY_ERR_INTERNAL;
            }
        }
        return anjay_ret_array_finish(array);
    }
    case ANJAY_DM_RID_ACCESS_CONTROL_OWNER:
        return anjay_ret_i32(ctx, (int32_t) inst->owner);
    default:
        ac_log(ERROR, "not implemented: get /2/%u/%u", iid, rid);
        return ANJAY_ERR_NOT_IMPLEMENTED;
    }
}

static int write_to_acl_array(AVS_LIST(acl_entry_t) *acl,
                              anjay_input_ctx_t *ctx) {
    int result;
    anjay_ssid_t ssid;
    int32_t mask;
    while ((result = anjay_get_array_index(ctx, &ssid)) == 0) {
        if (anjay_get_i32(ctx, &mask)) {
            return ANJAY_ERR_INTERNAL;
        }
        AVS_LIST(acl_entry_t) *it;
        AVS_LIST_FOREACH_PTR(it, acl) {
            if ((*it)->ssid == ssid) {
                (*it)->mask = (anjay_access_mask_t) mask;
                break;
            }
        }

        if (!*it) {
            AVS_LIST(acl_entry_t) new_entry = AVS_LIST_NEW_ELEMENT(acl_entry_t);
            if (!new_entry) {
                return ANJAY_ERR_INTERNAL;
            }
            *new_entry = (acl_entry_t) {
                .ssid = ssid,
                .mask = (anjay_access_mask_t) mask
            };
            AVS_LIST_INSERT(it, new_entry);
        }
    }
    if (result && result != ANJAY_GET_INDEX_END) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int ac_resource_write(anjay_t *anjay,
                             obj_ptr_t obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_input_ctx_t *ctx) {
    (void) anjay;
    access_control_t *access_control =
            _anjay_access_control_from_obj_ptr(obj_ptr);
    access_control_instance_t *inst = find_instance(access_control, iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case ANJAY_DM_RID_ACCESS_CONTROL_OID: {
        int32_t oid;
        int retval = anjay_get_i32(ctx, &oid);
        if (retval) {
            return retval;
        } else if (!_anjay_access_control_target_oid_valid(oid)) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->target.oid = (anjay_oid_t) oid;
        access_control->needs_validation = true;
        _anjay_access_control_mark_modified(access_control);
        return 0;
    }
    case ANJAY_DM_RID_ACCESS_CONTROL_OIID: {
        int32_t oiid;
        int retval = anjay_get_i32(ctx, &oiid);
        if (retval) {
            return retval;
        } else if (oiid < 0 || oiid > UINT16_MAX) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->target.iid = (anjay_iid_t) oiid;
        access_control->needs_validation = true;
        _anjay_access_control_mark_modified(access_control);
        return 0;
    }
    case ANJAY_DM_RID_ACCESS_CONTROL_ACL: {
        anjay_input_ctx_t *input_ctx = anjay_get_array(ctx);
        if (!input_ctx) {
            return ANJAY_ERR_INTERNAL;
        }

        AVS_LIST(acl_entry_t) new_acl = NULL;
        int retval = write_to_acl_array(&new_acl, input_ctx);
        if (!retval) {
            AVS_LIST_CLEAR(&inst->acl);
            inst->acl = new_acl;
            inst->has_acl = true;
            access_control->needs_validation = true;
            _anjay_access_control_mark_modified(access_control);
        } else {
            AVS_LIST_CLEAR(&new_acl);
        }
        return retval;
    }
    case ANJAY_DM_RID_ACCESS_CONTROL_OWNER: {
        int32_t ssid;
        int retval = anjay_get_i32(ctx, &ssid);
        if (retval) {
            return retval;
        } else if (ssid <= 0 || ssid > ANJAY_SSID_BOOTSTRAP) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->owner = (anjay_ssid_t) ssid;
        access_control->needs_validation = true;
        _anjay_access_control_mark_modified(access_control);
        return 0;
    }
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static void what_changed(anjay_ssid_t origin_ssid,
                         anjay_notify_queue_t queue,
                         bool *out_might_caused_orphaned_ac_instances,
                         bool *out_have_adds_or_removes) {
    *out_might_caused_orphaned_ac_instances = false;
    *out_have_adds_or_removes = false;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        if (!it->instance_set_changes.instance_set_changed) {
            continue;
        }
        if (it->oid == ANJAY_DM_OID_SECURITY || it->oid == ANJAY_DM_OID_SERVER
                || it->oid == ANJAY_DM_OID_ACCESS_CONTROL) {
            *out_might_caused_orphaned_ac_instances = true;
        }
        // NOTE: This makes it possible for BOOTSTRAP DELETE to leave
        // "lingering" Access Control instances without valid targets;
        // Relevant:
        // https://github.com/OpenMobileAlliance/OMA_LwM2M_for_Developers/issues/192
        //
        // Quoting Thierry's response:
        // > Regarding the orphan AC Object Instances, [...] it could be let
        // > implementation dependant. In LwM2M 1.1, the Boostrap Server should
        // > have better view on this, and could safely decide to take the
        // > responsibility to remove "lingering" ACO Instances.
        //
        // So in line with the spirit of letting the Bootstrap Server take care
        // of everything, we don't remove such "lingering" instances
        // automatically.
        if (origin_ssid != ANJAY_SSID_BOOTSTRAP
                && it->oid != ANJAY_DM_OID_ACCESS_CONTROL
                && (it->instance_set_changes.known_removed_iids
                    || it->instance_set_changes.known_added_iids)) {
            *out_have_adds_or_removes = true;
        }
        if (*out_might_caused_orphaned_ac_instances
                && *out_have_adds_or_removes) {
            // both flags possible to set are already set
            // they can't be any more true, so we break out from this loop
            break;
        }
    }
}

static int remove_ac_instance_by_target(anjay_t *anjay,
                                        access_control_t *ac,
                                        anjay_oid_t target_oid,
                                        anjay_iid_t target_iid,
                                        anjay_notify_queue_t *notify_queue) {
    AVS_LIST(access_control_instance_t) *it;
    AVS_LIST(access_control_instance_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(it, helper, &ac->current.instances) {
        if ((*it)->target.oid == target_oid
                && (*it)->target.iid == target_iid) {
            if (_anjay_dm_transaction_include_object(anjay, &ac->obj_def)
                    || _anjay_notify_queue_instance_removed(
                               notify_queue, ANJAY_DM_OID_ACCESS_CONTROL,
                               (*it)->iid)) {
                return -1;
            }
            AVS_LIST_CLEAR(&(*it)->acl);
            AVS_LIST_DELETE(it);
        }
    }
    return 0;
}

static int perform_adds_and_removes(anjay_t *anjay,
                                    access_control_t *ac,
                                    anjay_notify_queue_t incoming_queue,
                                    anjay_notify_queue_t *local_queue_ptr) {
    assert(_anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_ACCESS_CONTROL)
           == &ac->obj_def);

    const anjay_ssid_t origin_ssid = _anjay_dm_current_ssid(anjay);

    AVS_LIST(access_control_instance_t) acs_to_insert = NULL;
    AVS_LIST(access_control_instance_t) *acs_to_insert_append_ptr =
            &acs_to_insert;

    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, incoming_queue) {
        if (it->oid == ANJAY_DM_OID_ACCESS_CONTROL) {
            continue;
        }
        AVS_LIST(anjay_iid_t) iid_it;

        // remove Access Control object instances for removed instances
        AVS_LIST_FOREACH(iid_it, it->instance_set_changes.known_removed_iids) {
            int result = remove_ac_instance_by_target(anjay, ac, it->oid,
                                                      *iid_it, local_queue_ptr);
            if (result) {
                return result;
            }
        }

        // create Access Control object instances for created instances
        AVS_LIST_FOREACH(iid_it, it->instance_set_changes.known_added_iids) {
            if (_anjay_dm_transaction_include_object(anjay, &ac->obj_def)) {
                return -1;
            }
            AVS_LIST(access_control_instance_t) new_instance =
                    _anjay_access_control_create_missing_ac_instance(
                            origin_ssid,
                            &(const acl_target_t) {
                                .oid = it->oid,
                                .iid = *iid_it
                            });
            if (!AVS_LIST_INSERT(acs_to_insert_append_ptr, new_instance)) {
                return -1;
            }
            AVS_LIST_ADVANCE_PTR(&acs_to_insert_append_ptr);
        }
    }
    int result =
            _anjay_access_control_add_instances_without_iids(ac, &acs_to_insert,
                                                             local_queue_ptr);
    AVS_LIST_CLEAR(&acs_to_insert) {
        AVS_LIST_CLEAR(&acs_to_insert->acl);
    }
    return result;
}

static int sync_on_notify(anjay_t *anjay,
                          anjay_notify_queue_t incoming_queue,
                          void *data) {
    access_control_t *ac = (access_control_t *) data;
    if (ac->sync_in_progress) {
        return 0;
    }

    bool might_caused_orphaned_ac_instances;
    bool have_adds_or_removes;
    what_changed(_anjay_dm_current_ssid(anjay), incoming_queue,
                 &might_caused_orphaned_ac_instances, &have_adds_or_removes);
    if (!might_caused_orphaned_ac_instances && !have_adds_or_removes) {
        return 0;
    }

    int result = 0;
    ac->sync_in_progress = true;
    _anjay_dm_transaction_begin(anjay);
    anjay_notify_queue_t local_queue = NULL;
    if (might_caused_orphaned_ac_instances) {
        result = _anjay_access_control_remove_orphaned_instances(anjay, ac,
                                                                 &local_queue);
    }
    if (!result && have_adds_or_removes) {
        result = perform_adds_and_removes(anjay, ac, incoming_queue,
                                          &local_queue);
    }
    if (!result) {
        result = _anjay_notify_flush(anjay, &local_queue);
    } else {
        _anjay_notify_clear_queue(&local_queue);
    }
    result = _anjay_dm_transaction_finish(anjay, result);
    ac->sync_in_progress = false;
    return result;
}

static int ac_transaction_begin(anjay_t *anjay, obj_ptr_t obj_ptr) {
    (void) anjay;
    access_control_t *ac = _anjay_access_control_from_obj_ptr(obj_ptr);
    if (_anjay_access_control_clone_state(&ac->saved_state, &ac->current)) {
        ac_log(ERROR, "Out of memory");
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

static int acl_target_cmp(const void *left_, const void *right_) {
    const acl_target_t *left = (const acl_target_t *) left_;
    const acl_target_t *right = (const acl_target_t *) right_;
    if (left->oid != right->oid) {
        return left->oid - right->oid;
    } else {
        return left->iid - right->iid;
    }
}

static int validate_inst_ref(anjay_t *anjay,
                             AVS_RBTREE(acl_target_t) encountered_refs,
                             const acl_target_t *target) {
    ac_log(TRACE, "Validating: /%" PRIu16 "/%" PRId32, target->oid,
           target->iid);
    if (!_anjay_access_control_target_oid_valid(target->oid)
            || !_anjay_access_control_target_iid_valid(target->iid)) {
        ac_log(ERROR,
               "Validation failed: invalid target: /%" PRIu16 "/%" PRId32
               ": invalid IDs",
               target->oid, target->iid);
        return -1;
    }
    obj_ptr_t obj = _anjay_dm_find_object_by_oid(anjay, target->oid);
    if (!obj) {
        ac_log(ERROR,
               "Validation failed: invalid target: /%" PRIu16 "/%" PRId32
               ": no such object",
               target->oid, target->iid);
        return -1;
    }
    AVS_RBTREE_ELEM(acl_target_t) ref = AVS_RBTREE_ELEM_NEW(acl_target_t);
    if (!ref) {
        ac_log(ERROR, "Out of memory");
        return -1;
    }
    *ref = *target;
    if (AVS_RBTREE_INSERT(encountered_refs, ref) != ref) {
        // duplicate
        AVS_RBTREE_ELEM_DELETE_DETACHED(&ref);
        ac_log(ERROR,
               "Validation failed: duplicate target: /%" PRIu16 "/%" PRId32,
               target->oid, target->iid);
        return -1;
    }
    if (target->iid != ANJAY_IID_INVALID // ACL targeting object are always OK
            && _anjay_dm_instance_present(anjay, obj, (anjay_iid_t) target->iid,
                                          NULL)
                           <= 0) {
        ac_log(ERROR,
               "Validation failed: invalid target: /%" PRIu16 "/%" PRId32
               ": no such instance",
               target->oid, target->iid);
        return -1;
    }
    return 0;
}

static int anjay_ssid_cmp(const void *left, const void *right) {
    return *(const anjay_ssid_t *) left - *(const anjay_ssid_t *) right;
}

static int add_ssid(AVS_RBTREE(anjay_ssid_t) ssids_list, anjay_ssid_t ssid) {
    // here it is actually more likely for the SSID to be already present
    // so we use find-then-insert logic to avoid unnecessary allocations
    AVS_RBTREE_ELEM(anjay_ssid_t) elem = AVS_RBTREE_FIND(ssids_list, &ssid);
    if (!elem) {
        if (!(elem = AVS_RBTREE_ELEM_NEW(anjay_ssid_t))) {
            ac_log(ERROR, "Out of memory");
            return -1;
        }
        *elem = ssid;
        if (AVS_RBTREE_INSERT(ssids_list, elem) != elem) {
            AVS_UNREACHABLE("Internal error: cannot add tree element");
        }
    }
    return 0;
}

/**
 * Validates that <c>ssid</c> can be used as a key (RIID) in the ACL - it needs
 * to either reference a valid server, or be equal to @ref ANJAY_SSID_ANY (0).
 */
int _anjay_access_control_validate_ssid(anjay_t *anjay, anjay_ssid_t ssid) {
    return (ssid != ANJAY_SSID_BOOTSTRAP
            && (ssid == ANJAY_SSID_ANY || _anjay_dm_ssid_exists(anjay, ssid)))
                   ? 0
                   : -1;
}

static int ac_transaction_validate(anjay_t *anjay, obj_ptr_t obj_ptr) {
    access_control_t *access_control =
            _anjay_access_control_from_obj_ptr(obj_ptr);
    int result = 0;
    AVS_RBTREE(acl_target_t) encountered_refs = NULL;
    AVS_RBTREE(anjay_ssid_t) ssids_used = NULL;
    if (access_control->needs_validation) {
        if (!(encountered_refs = AVS_RBTREE_NEW(acl_target_t, acl_target_cmp))
                || !(ssids_used =
                             AVS_RBTREE_NEW(anjay_ssid_t, anjay_ssid_cmp))) {
            ac_log(ERROR, "Out of memory");
            goto finish;
        }
        access_control_instance_t *inst;
        result = ANJAY_ERR_BAD_REQUEST;
        AVS_LIST_FOREACH(inst, access_control->current.instances) {
            if (validate_inst_ref(anjay, encountered_refs, &inst->target)
                    || (inst->owner != ANJAY_SSID_BOOTSTRAP
                        && add_ssid(ssids_used, inst->owner))) {
                goto finish;
            }
            acl_entry_t *acl;
            AVS_LIST_FOREACH(acl, inst->acl) {
                if (add_ssid(ssids_used, acl->ssid)) {
                    goto finish;
                }
            }
        }
        AVS_RBTREE_DELETE(&ssids_used) {
            if (_anjay_access_control_validate_ssid(anjay, **ssids_used)) {
                ac_log(ERROR, "Validation failed: invalid SSID: %" PRIu16,
                       **ssids_used);
                goto finish;
            }
        }
        result = 0;
        access_control->needs_validation = false;
    }
finish:
    AVS_RBTREE_DELETE(&encountered_refs);
    AVS_RBTREE_DELETE(&ssids_used);
    return result;
}

static int ac_transaction_commit(anjay_t *anjay, obj_ptr_t obj_ptr) {
    (void) anjay;
    access_control_t *ac = _anjay_access_control_from_obj_ptr(obj_ptr);
    _anjay_access_control_clear_state(&ac->saved_state);
    ac->needs_validation = false;
    return 0;
}

static int ac_transaction_rollback(anjay_t *anjay, obj_ptr_t obj_ptr) {
    (void) anjay;
    access_control_t *ac = _anjay_access_control_from_obj_ptr(obj_ptr);
    _anjay_access_control_clear_state(&ac->current);
    ac->current = ac->saved_state;
    memset(&ac->saved_state, 0, sizeof(ac->saved_state));
    ac->needs_validation = false;
    return 0;
}

static void ac_delete(anjay_t *anjay, void *access_control_) {
    (void) anjay;
    access_control_t *access_control = (access_control_t *) access_control_;
    _anjay_access_control_clear_state(&access_control->current);
    _anjay_access_control_clear_state(&access_control->saved_state);
    avs_free(access_control);
}

void anjay_access_control_purge(anjay_t *anjay) {
    assert(anjay);
    access_control_t *ac = _anjay_access_control_get(anjay);
    _anjay_access_control_clear_state(&ac->current);
    _anjay_access_control_mark_modified(ac);
    ac->needs_validation = false;
    if (anjay_notify_instances_changed(anjay, ANJAY_DM_OID_ACCESS_CONTROL)) {
        ac_log(WARNING, "Could not schedule access control instance changes "
                        "notifications");
    }
}

bool anjay_access_control_is_modified(anjay_t *anjay) {
    assert(anjay);
    return _anjay_access_control_get(anjay)->current.modified_since_persist;
}

static const anjay_dm_module_t ACCESS_CONTROL_MODULE = {
    .notify_callback = sync_on_notify,
    .deleter = ac_delete
};

static const anjay_dm_object_def_t ACCESS_CONTROL = {
    .oid = ANJAY_DM_OID_ACCESS_CONTROL,
    .supported_rids =
            ANJAY_DM_SUPPORTED_RIDS(ANJAY_DM_RID_ACCESS_CONTROL_OID,
                                    ANJAY_DM_RID_ACCESS_CONTROL_OIID,
                                    ANJAY_DM_RID_ACCESS_CONTROL_ACL,
                                    ANJAY_DM_RID_ACCESS_CONTROL_OWNER),
    .handlers = {
        .instance_it = ac_instance_it,
        .instance_present = ac_instance_present,
        .instance_reset = ac_instance_reset,
        .instance_create = ac_instance_create,
        .instance_remove = ac_instance_remove,
        .resource_present = ac_resource_present,
        .resource_operations = ac_resource_operations,
        .resource_read = ac_resource_read,
        .resource_write = ac_resource_write,
        .transaction_begin = ac_transaction_begin,
        .transaction_validate = ac_transaction_validate,
        .transaction_commit = ac_transaction_commit,
        .transaction_rollback = ac_transaction_rollback
    }
};

int anjay_access_control_install(anjay_t *anjay) {
    if (!anjay) {
        ac_log(ERROR, "ANJAY object must not be NULL");
        return -1;
    }
    access_control_t *access_control =
            (access_control_t *) avs_calloc(1, sizeof(access_control_t));
    if (!access_control) {
        return -1;
    }
    access_control->obj_def = &ACCESS_CONTROL;
    if (_anjay_dm_module_install(anjay, &ACCESS_CONTROL_MODULE,
                                 access_control)) {
        avs_free(access_control);
        return -1;
    }
    if (anjay_register_object(anjay, &access_control->obj_def)) {
        // this will free access_control
        int result = _anjay_dm_module_uninstall(anjay, &ACCESS_CONTROL_MODULE);
        assert(!result);
        (void) result;
        return -1;
    }
    return 0;
}

access_control_t *_anjay_access_control_get(anjay_t *anjay) {
    return (access_control_t *) _anjay_dm_module_get_arg(
            anjay, &ACCESS_CONTROL_MODULE);
}
