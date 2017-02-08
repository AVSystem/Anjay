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
#include <string.h>

#include <avsystem/commons/rbtree.h>

#include <anjay_modules/notify.h>

#include "access_control.h"

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
    access_control_t *access_control = _anjay_access_control_get(obj_ptr);
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
    *out = curr ? curr->iid : ANJAY_IID_INVALID;
    *cookie = curr;

    return 0;
}

static int ac_instance_present(anjay_t *anjay,
                               obj_ptr_t obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    return find_instance(_anjay_access_control_get(obj_ptr), iid) != NULL;
}

static int ac_instance_reset(anjay_t *anjay,
                             obj_ptr_t obj_ptr,
                             anjay_iid_t iid) {
    (void) anjay;
    access_control_t *access_control = _anjay_access_control_get(obj_ptr);
    access_control_instance_t *inst = find_instance(access_control, iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }
    AVS_LIST_CLEAR(&inst->acl);
    inst->has_acl = false;
    inst->owner = 0;
    access_control->needs_validation = true;
    return 0;
}

static int ac_instance_create(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t *inout_iid,
                              anjay_ssid_t ssid) {
    (void) anjay;
    access_control_t *access_control = _anjay_access_control_get(obj_ptr);
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
        .owner   = ssid,
        .has_acl = false,
        .acl     = NULL
    };
    int retval = _anjay_access_control_add_instance(access_control,
                                                    new_instance);
    if (retval) {
        AVS_LIST_CLEAR(&new_instance);
    }
    access_control->needs_validation = true;
    return retval;
}

static int ac_instance_remove(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid) {
    (void) anjay;
    access_control_t *access_control = _anjay_access_control_get(obj_ptr);
    AVS_LIST(access_control_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &access_control->current.instances) {
        if ((*it)->iid == iid) {
            AVS_LIST_CLEAR(&(*it)->acl);
            AVS_LIST_DELETE(it);
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
                find_instance(_anjay_access_control_get(obj_ptr), iid);
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
            find_instance(_anjay_access_control_get(obj_ptr), iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case ANJAY_DM_RID_ACCESS_CONTROL_OID:
        return anjay_ret_i32(ctx, (int32_t) inst->target.oid);
    case ANJAY_DM_RID_ACCESS_CONTROL_OIID:
        return anjay_ret_i32(ctx, (int32_t) inst->target.iid);
    case ANJAY_DM_RID_ACCESS_CONTROL_ACL:
        {
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
            AVS_LIST(acl_entry_t) new_entry =
                AVS_LIST_NEW_ELEMENT(acl_entry_t);
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
    access_control_t *access_control = _anjay_access_control_get(obj_ptr);
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
        return 0;
    }
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static bool instances_changed_for(anjay_notify_queue_t queue, anjay_oid_t oid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        if (it->oid > oid) {
            break;
        } else if (it->oid == oid) {
            return it->instance_set_changes.instance_set_changed;
        }
    }
    return false;
}

static bool servers_might_have_changed(anjay_notify_queue_t queue) {
    return instances_changed_for(queue, ANJAY_DM_OID_SECURITY)
            || instances_changed_for(queue, ANJAY_DM_OID_SERVER)
            || instances_changed_for(queue, ANJAY_DM_OID_ACCESS_CONTROL);
}

static int append_oid(AVS_LIST(anjay_oid_t) **tail_ptr_ptr, anjay_oid_t oid) {
    if (_anjay_access_control_target_oid_valid(oid)) {
        assert(!**tail_ptr_ptr);
        if (!AVS_LIST_INSERT_NEW(anjay_oid_t, *tail_ptr_ptr)) {
            ac_log(ERROR, "Out of memory");
            return -1;
        }
        ***tail_ptr_ptr = oid;
        *tail_ptr_ptr = AVS_LIST_NEXT_PTR(*tail_ptr_ptr);
    }
    return 0;
}

static int
append_object_oid(anjay_t *anjay, obj_ptr_t obj, void *tail_ptr_ptr) {
    (void) anjay;
    return append_oid((AVS_LIST(anjay_oid_t) **) tail_ptr_ptr, (*obj)->oid);
}

static int enumerate_oids_to_sync(anjay_t *anjay,
                                  AVS_LIST(anjay_oid_t) *out_oids,
                                  anjay_notify_queue_t notify_queue) {
    AVS_LIST(anjay_oid_t) *tail_ptr = out_oids;
    if (instances_changed_for(notify_queue, ANJAY_DM_OID_ACCESS_CONTROL)) {
        // something changed in Access Control, sync everything
        return _anjay_dm_foreach_object(anjay, append_object_oid, &tail_ptr);
    } else {
        // sync only what changed
        AVS_LIST(anjay_notify_queue_object_entry_t) it;
        AVS_LIST_FOREACH(it, notify_queue) {
            if (it->instance_set_changes.instance_set_changed) {
                int result = append_oid(&tail_ptr, it->oid);
                if (result) {
                    return result;
                }
            }
        }
        return 0;
    }
}


static int sync_on_notify(anjay_t *anjay,
                          anjay_ssid_t origin_ssid,
                          anjay_notify_queue_t incoming_queue,
                          void *data) {
    access_control_t *ac = _anjay_access_control_get((obj_ptr_t) data);
    if (ac->sync_in_progress) {
        return 0;
    }
    ac->sync_in_progress = true;
    _anjay_dm_transaction_begin(anjay);
    AVS_LIST(anjay_oid_t) oids_to_sync = NULL;
    int result;
    anjay_notify_queue_t local_queue = NULL;
    if ((result = enumerate_oids_to_sync(anjay, &oids_to_sync, incoming_queue))
            || (result = _anjay_access_control_sync_instances(
                    ac, origin_ssid, oids_to_sync, &local_queue))) {
        goto finish;
    }
    if (servers_might_have_changed(incoming_queue)) {
        result = _anjay_access_control_remove_orphaned_instances(ac,
                                                                 &local_queue);
    }
finish:
    AVS_LIST_CLEAR(&oids_to_sync);
    if (!result) {
        result = _anjay_notify_flush(anjay, origin_ssid, &local_queue);
    } else {
        _anjay_notify_clear_queue(&local_queue);
    }
    result = _anjay_dm_transaction_finish(anjay, result);
    ac->sync_in_progress = false;
    return result;
}

static int ac_on_register(anjay_t *anjay,
                          obj_ptr_t obj_ptr) {
    return _anjay_notify_register_callback(anjay, sync_on_notify,
                                           (void *) (intptr_t) obj_ptr);
}

static int ac_transaction_begin(anjay_t *anjay, obj_ptr_t obj_ptr) {
    (void) anjay;
    access_control_t *ac = _anjay_access_control_get(obj_ptr);
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
    ac_log(TRACE, "Validating: /%" PRIu16 "/%" PRIu16,
           target->oid, target->iid);
    if (!_anjay_access_control_target_oid_valid(target->oid)
            || !_anjay_access_control_target_iid_valid(target->iid)) {
        ac_log(ERROR, "Validation failed: invalid target: "
               "/%" PRIu16 "/%" PRId32 ": invalid IDs",
               target->oid, target->iid);
        return -1;
    }
    obj_ptr_t obj = _anjay_dm_find_object_by_oid(anjay, target->oid);
    if (!obj) {
        ac_log(ERROR, "Validation failed: invalid target: "
               "/%" PRIu16 "/%" PRId32 ": no such object",
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
        ac_log(ERROR, "Validation failed: duplicate target: "
               "/%" PRIu16 "/%" PRId32, target->oid, target->iid);
        return -1;
    }
    if (target->iid != ANJAY_IID_INVALID // ACL targeting object are always OK
            && _anjay_dm_instance_present(anjay, obj,
                                          (anjay_iid_t) target->iid) <= 0) {
        ac_log(ERROR, "Validation failed: invalid target: "
               "/%" PRIu16 "/%" PRId32 ": no such instance",
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
            assert(0 && "Internal error: cannot add tree element");
        }
    }
    return 0;
}

int _anjay_access_control_validate_ssid(anjay_t *anjay, anjay_ssid_t ssid) {
    return (ssid != 0
                    && (ssid == ANJAY_SSID_BOOTSTRAP
                            || _anjay_dm_ssid_exists(anjay, ssid)))
            ? 0 : -1;
}

static int
ac_transaction_validate(anjay_t *anjay, obj_ptr_t obj_ptr) {
    access_control_t *access_control = _anjay_access_control_get(obj_ptr);
    int result = 0;
    AVS_RBTREE(acl_target_t) encountered_refs = NULL;
    AVS_RBTREE(anjay_ssid_t) ssids_used = NULL;
    if (access_control->needs_validation) {
        if (!(encountered_refs = AVS_RBTREE_NEW(acl_target_t, acl_target_cmp))
                || !(ssids_used = AVS_RBTREE_NEW(anjay_ssid_t,
                                                 anjay_ssid_cmp))) {
            ac_log(ERROR, "Out of memory");
            goto finish;
        }
        access_control_instance_t *inst;
        result = ANJAY_ERR_BAD_REQUEST;
        AVS_LIST_FOREACH(inst, access_control->current.instances) {
            if (validate_inst_ref(anjay, encountered_refs, &inst->target)
                    || add_ssid(ssids_used, inst->owner)) {
                goto finish;
            }
            acl_entry_t *acl;
            AVS_LIST_FOREACH(acl, inst->acl) {
                if (acl->ssid != 0 && add_ssid(ssids_used, inst->owner)) {
                    goto finish;
                }
            }
        }
        AVS_RBTREE_DELETE(&ssids_used) {
            if (_anjay_access_control_validate_ssid(anjay, **ssids_used)) {
                ac_log(ERROR, "Validation failed: invalid SSID: %" PRIu16,
                       inst->owner);
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
    access_control_t *ac = _anjay_access_control_get(obj_ptr);
    _anjay_access_control_clear_state(&ac->saved_state);
    ac->needs_validation = false;
    return 0;
}

static int ac_transaction_rollback(anjay_t *anjay, obj_ptr_t obj_ptr) {
    (void) anjay;
    access_control_t *ac = _anjay_access_control_get(obj_ptr);
    _anjay_access_control_clear_state(&ac->current);
    ac->current = ac->saved_state;
    memset(&ac->saved_state, 0, sizeof(ac->saved_state));
    ac->needs_validation = false;
    return 0;
}

obj_ptr_t anjay_access_control_object_new(anjay_t *anjay) {
    if (!anjay) {
        ac_log(ERROR, "ANJAY object must not be NULL");
        return NULL;
    }
    static const anjay_dm_object_def_t ACCESS_CONTROL = {
        .oid = ANJAY_DM_OID_ACCESS_CONTROL,
        .rid_bound = 4,
        .instance_it = ac_instance_it,
        .instance_present = ac_instance_present,
        .instance_reset = ac_instance_reset,
        .instance_create = ac_instance_create,
        .instance_remove = ac_instance_remove,
        .resource_present = ac_resource_present,
        .resource_supported = anjay_dm_resource_supported_TRUE,
        .resource_operations = ac_resource_operations,
        .resource_read = ac_resource_read,
        .resource_write = ac_resource_write,
        .on_register = ac_on_register,
        .transaction_begin = ac_transaction_begin,
        .transaction_validate = ac_transaction_validate,
        .transaction_commit = ac_transaction_commit,
        .transaction_rollback = ac_transaction_rollback
    };
    access_control_t *access_control =
        (access_control_t *) calloc(1, sizeof(access_control_t));
    if (!access_control) {
        return NULL;
    }
    access_control->obj_def = &ACCESS_CONTROL;
    access_control->anjay = anjay;
    return &access_control->obj_def;
}
