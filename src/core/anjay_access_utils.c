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

#include <inttypes.h>

#include <anjay_modules/anjay_access_utils.h>
#include <anjay_modules/anjay_raw_buffer.h>

#include "anjay_access_utils_private.h"
#include "anjay_dm_core.h"
#include "anjay_io_core.h"
#include "anjay_servers_utils.h"

#include "io/anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

#ifdef ANJAY_WITH_ACCESS_CONTROL

static inline const anjay_dm_object_def_t *const *
get_access_control(anjay_t *anjay) {
    return _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_ACCESS_CONTROL);
}

static int
read_u16(anjay_t *anjay, anjay_iid_t iid, anjay_rid_t rid, uint16_t *out) {
    int64_t ret;
    const anjay_uri_path_t uri =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_ACCESS_CONTROL, iid, rid);
    int result = _anjay_dm_read_resource_i64(anjay, &uri, &ret);
    if (result) {
        return result;
    } else if (ret != (uint16_t) ret) {
        anjay_log(WARNING,
                  _("cannot read ") "%s" _(" = ") "%s" _(
                          " as uint16: value overflow"),
                  ANJAY_DEBUG_MAKE_PATH(&uri), AVS_INT64_AS_STRING(ret));
        return -1;
    }
    *out = (uint16_t) ret;
    return 0;
}

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    uint16_t value;
} u16_writer_ctx_t;

static int u16_writer_integer(anjay_input_ctx_t *ctx, int64_t *out) {
    *out = ((u16_writer_ctx_t *) ctx)->value;
    return 0;
}

static int write_u16(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *ac_obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_riid_t riid,
                     uint16_t value) {
    static const anjay_input_ctx_vtable_t VTABLE = {
        .integer = u16_writer_integer
    };
    u16_writer_ctx_t ctx = {
        .vtable = &VTABLE,
        .value = value
    };
    return _anjay_dm_call_resource_write(anjay, ac_obj_ptr, iid, rid, riid,
                                         (anjay_input_ctx_t *) &ctx, NULL);
}

static int read_ids_from_ac_instance(anjay_t *anjay,
                                     anjay_iid_t access_control_iid,
                                     anjay_oid_t *out_oid,
                                     anjay_iid_t *out_oiid,
                                     anjay_ssid_t *out_owner) {
    int ret = 0;
    if (!ret && out_oid) {
        ret = read_u16(anjay, access_control_iid,
                       ANJAY_DM_RID_ACCESS_CONTROL_OID, out_oid);
    }
    if (!ret && out_oiid) {
        ret = read_u16(anjay, access_control_iid,
                       ANJAY_DM_RID_ACCESS_CONTROL_OIID, out_oiid);
    }
    if (!ret && out_owner) {
        ret = read_u16(anjay, access_control_iid,
                       ANJAY_DM_RID_ACCESS_CONTROL_OWNER, out_owner);
    }
    return ret;
}

static int read_mask(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_riid_t riid,
                     anjay_access_mask_t *out) {
    int64_t mask;

    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, &mask, sizeof(mask));
    anjay_output_buf_ctx_t ctx =
            _anjay_output_buf_ctx_init((avs_stream_t *) &stream);
    int result =
            _anjay_dm_call_resource_read(anjay, obj, iid, rid, riid,
                                         (anjay_output_ctx_t *) &ctx, NULL);
    if (!result) {
        if (avs_stream_outbuf_offset(&stream) != sizeof(mask)) {
            return -1;
        }
        *out = (anjay_access_mask_t) mask;
    }
    return result;
}

typedef struct {
    bool acl_empty;
    anjay_ssid_t ssid_lookup;
    anjay_ssid_t found_ssid;
    anjay_access_mask_t mask;
} get_mask_instance_clb_args_t;

static int get_mask_instance_clb(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 void *args_) {
    get_mask_instance_clb_args_t *args = (get_mask_instance_clb_args_t *) args_;

    args->acl_empty = false;
    if (riid == args->ssid_lookup || riid == 0) {
        // Found an entry for the given ssid or the default ACL entry
        anjay_access_mask_t mask;
        int result = read_mask(anjay, obj, iid, rid, riid, &mask);
        if (result) {
            return result;
        }

        args->found_ssid = riid;
        args->mask = mask;
        if (riid) {
            // not the default
            return ANJAY_FOREACH_BREAK;
        }
    }
    return 0;
}

static int foreach_acl(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *ac_obj,
                       anjay_iid_t ac_iid,
                       anjay_dm_foreach_resource_instance_handler_t *handler,
                       void *data) {
    anjay_dm_resource_kind_t kind;
    anjay_dm_resource_presence_t presence;
    int result = _anjay_dm_resource_kind_and_presence(
            anjay, ac_obj, ac_iid, ANJAY_DM_RID_ACCESS_CONTROL_ACL, &kind,
            &presence);
    if (!result && presence != ANJAY_DM_RES_ABSENT) {
        // Ensure that the ACL resource is sane: readable and multi-instance;
        // it might not be true if Access Control object is user-implemented
        // in an insane way, and without checking this, calling
        // list_resource_instances or later resource_read
        // would break handler contracts
        if (!_anjay_dm_res_kind_readable(kind)
                || !_anjay_dm_res_kind_multiple(kind)) {
            return -1;
        }
        result = _anjay_dm_foreach_resource_instance(
                anjay, ac_obj, ac_iid, ANJAY_DM_RID_ACCESS_CONTROL_ACL, handler,
                data);
    }
    return result;
}

typedef struct {
    anjay_iid_t ac_iid;
    anjay_oid_t target_oid;
    anjay_iid_t target_iid;
} find_ac_instance_args_t;

static int find_ac_instance_clb(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *ac_obj,
                                anjay_iid_t ac_iid,
                                void *args_) {
    (void) ac_obj;
    find_ac_instance_args_t *args = (find_ac_instance_args_t *) args_;
    anjay_oid_t res_oid;
    anjay_iid_t res_oiid;
    int result =
            read_ids_from_ac_instance(anjay, ac_iid, &res_oid, &res_oiid, NULL);
    if (result) {
        return result;
    }
    if (res_oid == args->target_oid && res_oiid == args->target_iid) {
        assert(args->ac_iid == ANJAY_ID_INVALID);
        args->ac_iid = ac_iid;
        return ANJAY_FOREACH_BREAK;
    }
    return ANJAY_FOREACH_CONTINUE;
}

static int
find_ac_instance_by_target(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *ac_obj,
                           anjay_iid_t *out_ac_iid,
                           anjay_oid_t target_oid,
                           anjay_iid_t target_iid) {
    find_ac_instance_args_t args = {
        .ac_iid = ANJAY_ID_INVALID,
        .target_oid = target_oid,
        .target_iid = target_iid
    };
    int result = _anjay_dm_foreach_instance(anjay, ac_obj, find_ac_instance_clb,
                                            &args);
    if (!result) {
        if (args.ac_iid == ANJAY_ID_INVALID) {
            return ANJAY_ERR_NOT_FOUND;
        }
        if (out_ac_iid) {
            *out_ac_iid = args.ac_iid;
        }
    }
    return result;
}

static int get_mask(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *ac_obj,
                    anjay_iid_t ac_iid,
                    anjay_ssid_t *inout_ssid,
                    anjay_access_mask_t *out_mask) {
    get_mask_instance_clb_args_t args = {
        .acl_empty = true,
        .ssid_lookup = *inout_ssid,
        .found_ssid = 0,
        .mask = ANJAY_ACCESS_MASK_NONE
    };
    int result =
            foreach_acl(anjay, ac_obj, ac_iid, get_mask_instance_clb, &args);
    if (result) {
        return result;
    }
    if (args.acl_empty) {
        *inout_ssid = ANJAY_SSID_BOOTSTRAP;
    } else {
        *inout_ssid = args.found_ssid;
    }
    *out_mask = args.mask;
    return 0;
}

static anjay_access_mask_t access_control_mask(anjay_t *anjay,
                                               anjay_oid_t oid,
                                               anjay_iid_t iid,
                                               anjay_ssid_t ssid) {
    const anjay_dm_object_def_t *const *ac_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_ACCESS_CONTROL);
    anjay_iid_t ac_iid;
    if (!ac_obj
            || find_ac_instance_by_target(anjay, ac_obj, &ac_iid, oid, iid)) {
        return ANJAY_ACCESS_MASK_NONE;
    }

    anjay_ssid_t found_ssid = ssid;
    anjay_access_mask_t mask;
    if (get_mask(anjay, ac_obj, (anjay_iid_t) ac_iid, &found_ssid, &mask)) {
        anjay_log(WARNING, _("failed to read ACL!"));
        return ANJAY_ACCESS_MASK_NONE;
    }

    if (found_ssid == ssid) {
        // Found the ACL
        return mask;
    } else if (found_ssid == ANJAY_SSID_BOOTSTRAP) {
        anjay_ssid_t owner;
        if (!read_ids_from_ac_instance(anjay, ac_iid, NULL, NULL, &owner)
                && owner == ssid) {
            // Empty ACL, and given ssid is an owner of the instance
            return ANJAY_ACCESS_MASK_FULL & ~ANJAY_ACCESS_MASK_CREATE;
        }
    } else if (!found_ssid) {
        // Default ACL
        return mask;
    }
    return ANJAY_ACCESS_MASK_NONE;
}

static bool can_instantiate(anjay_t *anjay, const anjay_action_info_t *info) {
    return access_control_mask(anjay, info->oid, ANJAY_ID_INVALID, info->ssid)
           & ANJAY_ACCESS_MASK_CREATE;
}

typedef struct {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_ssid_t owner;
} get_owner_data_t;

static int
count_non_bootstrap_clb(anjay_t *anjay, anjay_ssid_t ssid, void *counter_ptr) {
    (void) anjay;
    if (ssid != ANJAY_SSID_BOOTSTRAP) {
        ++*((size_t *) counter_ptr);
    }
    return ANJAY_FOREACH_CONTINUE;
}

static bool is_single_ssid_environment(anjay_t *anjay) {
    size_t non_bootstrap_count = 0;
    if (_anjay_servers_foreach_ssid(anjay, count_non_bootstrap_clb,
                                    &non_bootstrap_count)) {
        return false;
    }
    return non_bootstrap_count == 1;
}

#endif // ANJAY_WITH_ACCESS_CONTROL

bool _anjay_instance_action_allowed(anjay_t *anjay,
                                    const anjay_action_info_t *info) {
    assert(info->oid != ANJAY_DM_OID_SECURITY);
    assert(info->iid != ANJAY_ID_INVALID
           || info->action == ANJAY_ACTION_CREATE);
#ifndef ANJAY_WITH_ACCESS_CONTROL
    return true;
#else
    if (info->ssid == ANJAY_SSID_BOOTSTRAP) {
        // Access Control is not applicable to Bootstrap Server
        return true;
    }

    if (info->action == ANJAY_ACTION_DISCOVER) {
        return true;
    }

    if (!get_access_control(anjay) || is_single_ssid_environment(anjay)) {
        return true;
    }

    if (info->oid == ANJAY_DM_OID_ACCESS_CONTROL) {
        if (info->action == ANJAY_ACTION_READ
                || info->action == ANJAY_ACTION_WRITE_ATTRIBUTES) {
            return true;
        } else if (info->action == ANJAY_ACTION_CREATE
                   || info->action == ANJAY_ACTION_DELETE) {
            return false;
        }
        anjay_ssid_t owner;
        if (read_u16(anjay, info->iid, ANJAY_DM_RID_ACCESS_CONTROL_OWNER,
                     &owner)) {
            return false;
        }
        return owner == info->ssid;
    }

    if (info->action == ANJAY_ACTION_CREATE) {
        return can_instantiate(anjay, info);
    }

    anjay_access_mask_t mask =
            access_control_mask(anjay, info->oid, info->iid, info->ssid);
    switch (info->action) {
    case ANJAY_ACTION_READ:
    case ANJAY_ACTION_WRITE_ATTRIBUTES:
        return !!(mask & ANJAY_ACCESS_MASK_READ);
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
        return !!(mask & ANJAY_ACCESS_MASK_WRITE);
    case ANJAY_ACTION_EXECUTE:
        return !!(mask & ANJAY_ACCESS_MASK_EXECUTE);
    case ANJAY_ACTION_DELETE:
        return !!(mask & ANJAY_ACCESS_MASK_DELETE);
    default:
        AVS_UNREACHABLE("invalid enum value");
        return false;
    }
#endif // ANJAY_WITH_ACCESS_CONTROL
}

#ifdef ANJAY_WITH_ACCESS_CONTROL

static void what_changed(anjay_ssid_t origin_ssid,
                         anjay_notify_queue_t notifications_already_queued,
                         bool *out_might_caused_orphaned_ac_instances,
                         bool *out_have_adds,
                         bool *out_might_have_removes) {
    *out_might_caused_orphaned_ac_instances = false;
    *out_have_adds = false;
    *out_might_have_removes = false;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, notifications_already_queued) {
        if (!it->instance_set_changes.instance_set_changed) {
            continue;
        }
        if (it->oid == ANJAY_DM_OID_SECURITY || it->oid == ANJAY_DM_OID_SERVER
                || it->oid == ANJAY_DM_OID_ACCESS_CONTROL) {
            // If the instance set changed for Security or Server, the set of
            // valid SSIDs might have changed, so some AC instances might now
            // be orphaned (have no valid owner).
            // Also, if the set of Access Control instances changed, it might
            // mean that an instance with invalid owner (so, technically,
            // already orphaned) have been created.
            *out_might_caused_orphaned_ac_instances = true;
        }
        if (it->oid != ANJAY_DM_OID_SECURITY
                && it->oid != ANJAY_DM_OID_ACCESS_CONTROL) {
            *out_might_have_removes = true;
            if (it->instance_set_changes.known_added_iids
                    && origin_ssid != ANJAY_SSID_BOOTSTRAP) {
                // Technically, even if this condition is not met, there might
                // be "undocumented" adds (not listed in known_added_iids), but
                // we don't care about them.
                *out_have_adds = true;
            }
        }
        if (*out_might_caused_orphaned_ac_instances && *out_might_have_removes
                && (*out_have_adds || origin_ssid == ANJAY_SSID_BOOTSTRAP)) {
            // all flags possible to set are already set
            // they can't be any more true, so we break out from this loop
            break;
        }
    }
}

static int
enumerate_valid_ssids_clb(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *security_obj,
                          anjay_iid_t iid,
                          void *ssid_list_ptr_) {
    (void) security_obj;
    AVS_LIST(anjay_ssid_t) *insert_ptr =
            (AVS_LIST(anjay_ssid_t) *) ssid_list_ptr_;
    anjay_ssid_t ssid;
    if (_anjay_ssid_from_security_iid(anjay, iid, &ssid)) {
        return -1;
    }
    while (*insert_ptr && **insert_ptr < ssid) {
        AVS_LIST_ADVANCE_PTR(&insert_ptr);
    }
    if (*insert_ptr && **insert_ptr == ssid) {
        return 0;
    }
    if (!AVS_LIST_INSERT_NEW(anjay_ssid_t, insert_ptr)) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    **insert_ptr = ssid;
    return 0;
}

typedef struct {
    anjay_ssid_t ssid;
    anjay_access_mask_t mask;
} acl_entry_t;

static int read_acl_clb(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj,
                        anjay_iid_t iid,
                        anjay_rid_t rid,
                        anjay_riid_t riid,
                        void *endptr_ptr_) {
    AVS_LIST(acl_entry_t) **endptr_ptr = (AVS_LIST(acl_entry_t) **) endptr_ptr_;
    assert(!**endptr_ptr);
    if (!(**endptr_ptr = AVS_LIST_NEW_ELEMENT(acl_entry_t))) {
        return -1;
    }
    (**endptr_ptr)->ssid = riid;
    int result = read_mask(anjay, obj, iid, rid, riid, &(**endptr_ptr)->mask);
    if (result) {
        AVS_LIST_DELETE(*endptr_ptr);
    } else {
        AVS_LIST_ADVANCE_PTR(endptr_ptr);
    }
    return result;
}

static int read_acl(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *ac_obj,
                    anjay_iid_t ac_iid,
                    AVS_LIST(acl_entry_t) *out_acl) {
    assert(out_acl);
    assert(!*out_acl);
    AVS_LIST(acl_entry_t) *endptr = out_acl;
    int result = foreach_acl(anjay, ac_obj, ac_iid, read_acl_clb, &endptr);
    if (result) {
        AVS_LIST_CLEAR(out_acl);
    }
    return result;
}

/**
 * Finds the server that will become the new owner of the given ACL.
 * Servers with both Write and Delete rights are ranked with value 2, those with
 * one of these are ranked with 1, others with 0. The first entry with the
 * highest rank is elected the new owner.
 */
static anjay_ssid_t elect_instance_owner(AVS_LIST(acl_entry_t) acl) {
    static const size_t write_weight = 1;
    static const size_t delete_weight = 1;

    /* Clearly we cannot perform election otherwise. */
    assert(AVS_LIST_SIZE(acl) > 0);

    anjay_ssid_t new_owner = ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP;
    size_t highest_sum = 0;

    AVS_LIST(acl_entry_t) entry;
    AVS_LIST_FOREACH(entry, acl) {
        size_t sum = (entry->mask & ANJAY_ACCESS_MASK_WRITE) * write_weight
                     + (entry->mask & ANJAY_ACCESS_MASK_DELETE) * delete_weight;
        if (sum >= highest_sum) {
            highest_sum = sum;
            new_owner = entry->ssid;
        }
    }
    return new_owner;
}

typedef struct {
    anjay_iid_t ac_iid;
    anjay_oid_t target_oid;
    anjay_iid_t target_iid;
} orphaned_instance_info_t;

typedef struct {
    AVS_LIST(anjay_ssid_t) valid_ssids;
    AVS_LIST(orphaned_instance_info_t) *orphaned_instance_list_append_ptr;
    anjay_notify_queue_t *out_dm_changes;
} process_orphaned_instances_args_t;

static int
process_orphaned_instances_clb(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_iid_t iid,
                               void *args_) {
    process_orphaned_instances_args_t *args =
            (process_orphaned_instances_args_t *) args_;

    anjay_oid_t target_oid;
    anjay_iid_t target_iid;
    anjay_ssid_t owner;
    AVS_LIST(acl_entry_t) acl = NULL;
    bool acl_modified = false;
    bool owner_valid = true;
    AVS_LIST(acl_entry_t) *entry;
    AVS_LIST(acl_entry_t) acl_helper;
    int result = 0;

    // Read all resources in the Access Control instance
    if ((result = read_ids_from_ac_instance(anjay, iid, &target_oid,
                                            &target_iid, &owner))
            || (result = read_acl(anjay, obj, iid, &acl))) {
        goto finish;
    }

    // Remove invalid SSID from our temporary copy of the ACL
    AVS_LIST_DELETABLE_FOREACH_PTR(entry, acl_helper, &acl) {
        if ((*entry)->ssid != ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP
                && (*entry)->ssid != ANJAY_SSID_ANY
                && !AVS_LIST_FIND_BY_VALUE_PTR(&args->valid_ssids,
                                               &(*entry)->ssid, memcmp)) {
            acl_modified = true;
            if ((*entry)->ssid == owner) {
                owner_valid = false;
            }
            AVS_LIST_DELETE(entry);
        }
    }
    if (!acl_modified) {
        goto finish;
    }
    if (!acl) {
        // No valid ACL entries, the entire instance needs to be removed;
        // we can't do it now because of handler contracts, so add it to the
        // list of orphaned instances for later removal by
        // remove_orphaned_instances().
        assert(!*args->orphaned_instance_list_append_ptr);
        if (!(*args->orphaned_instance_list_append_ptr =
                      AVS_LIST_NEW_ELEMENT(orphaned_instance_info_t))) {
            anjay_log(ERROR, _("out of memory"));
            result = -1;
            goto finish;
        }
        (*args->orphaned_instance_list_append_ptr)->ac_iid = iid;
        (*args->orphaned_instance_list_append_ptr)->target_oid = target_oid;
        (*args->orphaned_instance_list_append_ptr)->target_iid = target_iid;
        AVS_LIST_ADVANCE_PTR(&args->orphaned_instance_list_append_ptr);
    } else {
        if (!owner_valid) {
            (void) ((result = write_u16(
                             anjay, obj, iid, ANJAY_DM_RID_ACCESS_CONTROL_OWNER,
                             ANJAY_ID_INVALID, elect_instance_owner(acl)))
                    || (result = _anjay_notify_queue_resource_change(
                                args->out_dm_changes,
                                ANJAY_DM_OID_ACCESS_CONTROL, iid,
                                ANJAY_DM_RID_ACCESS_CONTROL_OWNER)));
        }
        // Rewrite the modified ACL to the data model
        if (!result) {
            result = _anjay_dm_call_resource_reset(
                    anjay, obj, iid, ANJAY_DM_RID_ACCESS_CONTROL_ACL, NULL);
        }
        AVS_LIST_CLEAR(&acl) {
            if (!result) {
                result = write_u16(anjay, obj, iid,
                                   ANJAY_DM_RID_ACCESS_CONTROL_ACL, acl->ssid,
                                   acl->mask);
            }
        }
        if (!result) {
            result = _anjay_notify_queue_resource_change(
                    args->out_dm_changes, ANJAY_DM_OID_ACCESS_CONTROL, iid,
                    ANJAY_DM_RID_ACCESS_CONTROL_ACL);
        }
    }
finish:
    AVS_LIST_CLEAR(&acl);
    return result;
}

static int remove_referred_instance(anjay_t *anjay,
                                    const orphaned_instance_info_t *it,
                                    anjay_notify_queue_t *out_dm_changes) {
    // We do not fail if either of the following is true:
    // - the target Object does not exist
    // - the target Instance is not set
    // - the target Instance does not exist
    int result = 0;
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, it->target_oid);
    if (obj
            && _anjay_dm_instance_present(anjay, obj,
                                          (anjay_iid_t) it->target_iid)
                           > 0
            && !(result = _anjay_dm_call_instance_remove(
                         anjay, obj, (anjay_iid_t) it->target_iid, NULL))) {
        result = _anjay_notify_queue_instance_removed(
                out_dm_changes, it->target_oid, it->target_iid);
    }
    if (result) {
        anjay_log(ERROR,
                  _("cannot remove assigned Object Instance /") "%" PRIu16 _(
                          "/") "%" PRIu16,
                  it->target_oid, it->target_iid);
    }
    return result;
}

/**
 * Removes ACL entries (ACL Resource Instances) that refer to SSIDs that do not
 * correspond with any Security object instance.
 *
 * Also, if any Access Control object's owner is set to an SSID that is no
 * longer, valid:
 * - changes the owner of that ACL to some other server listed in the ACL if
 *   possible,
 * - if not, removes the Access Control instance, and the object instance
 *   referred to by it (see LwM2M TS 1.0.2, E.1.3 Unbootstrapping).
 */
static int
remove_orphaned_instances(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *ac_obj,
                          anjay_notify_queue_t *new_notifications_queue) {
    int result = 0;
    AVS_LIST(anjay_ssid_t) ssid_list = NULL;
    AVS_LIST(orphaned_instance_info_t) instances_to_remove = NULL;
    const anjay_dm_object_def_t *const *security_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (security_obj) {
        result = _anjay_dm_foreach_instance(
                anjay, security_obj, enumerate_valid_ssids_clb, &ssid_list);
    }
    if (!result) {
        result = _anjay_dm_foreach_instance(
                anjay, ac_obj, process_orphaned_instances_clb,
                &(process_orphaned_instances_args_t) {
                    .valid_ssids = ssid_list,
                    .orphaned_instance_list_append_ptr = &instances_to_remove,
                    .out_dm_changes = new_notifications_queue
                });
    }
    // Actually remove the instances marked by process_orphaned_instances_clb
    // as necessary for removal, and the Object Instances referred to by them.
    AVS_LIST_CLEAR(&instances_to_remove) {
        (void) (result
                || (result =
                            remove_referred_instance(anjay, instances_to_remove,
                                                     new_notifications_queue))
                || (result = _anjay_dm_call_instance_remove(
                            anjay, ac_obj, instances_to_remove->ac_iid, NULL))
                || (result = _anjay_notify_queue_instance_removed(
                            new_notifications_queue,
                            ANJAY_DM_OID_ACCESS_CONTROL,
                            instances_to_remove->ac_iid)));
    }
    AVS_LIST_CLEAR(&ssid_list);
    return result;
}

typedef struct anjay_acl_ref_validation_object_info_struct {
    anjay_oid_t oid;
    AVS_LIST(anjay_iid_t) allowed_iids;
} acl_ref_validation_object_info_t;

static AVS_LIST(anjay_iid_t)
create_allowed_iids_set(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj) {
    AVS_LIST(anjay_iid_t) result = NULL;
    if (_anjay_dm_get_sorted_instance_list(anjay, obj, &result)) {
        return NULL;
    }
    // ANJAY_ID_INVALID is also allowed for AC target
    AVS_LIST(anjay_iid_t) iid = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
    if (!iid) {
        AVS_LIST_CLEAR(&result);
        return NULL;
    }
    *iid = ANJAY_ID_INVALID;
    AVS_LIST_APPEND(&result, iid);
    return result;
}

static acl_ref_validation_object_info_t *
get_or_create_validation_object_info(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     anjay_acl_ref_validation_ctx_t *ctx) {
    AVS_LIST(acl_ref_validation_object_info_t) *it;
    AVS_LIST_FOREACH_PTR(it, &ctx->object_infos) {
        if ((*it)->oid == (*obj)->oid) {
            return *it;
        } else if ((*it)->oid > (*obj)->oid) {
            break;
        }
    }
    if (!AVS_LIST_INSERT_NEW(acl_ref_validation_object_info_t, it)) {
        return NULL;
    }
    (*it)->oid = (*obj)->oid;
    if (!((*it)->allowed_iids = create_allowed_iids_set(anjay, obj))) {
        AVS_LIST_DELETE(it);
        return NULL;
    }
    return *it;
}

void _anjay_acl_ref_validation_ctx_cleanup(
        anjay_acl_ref_validation_ctx_t *ctx) {
    AVS_LIST_CLEAR(&ctx->object_infos) {
        AVS_LIST_CLEAR(&ctx->object_infos->allowed_iids);
    }
}

int _anjay_acl_ref_validate_inst_ref(anjay_t *anjay,
                                     anjay_acl_ref_validation_ctx_t *ctx,
                                     anjay_oid_t target_oid,
                                     anjay_iid_t target_iid) {
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, target_oid);
    if (!obj) {
        return -1;
    }
    acl_ref_validation_object_info_t *object_info =
            get_or_create_validation_object_info(anjay, obj, ctx);
    if (!object_info) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    AVS_LIST(anjay_iid_t) *allowed_iid_ptr = &object_info->allowed_iids;
    while (*allowed_iid_ptr && **allowed_iid_ptr < target_iid) {
        AVS_LIST_ADVANCE_PTR(&allowed_iid_ptr);
    }
    if (!*allowed_iid_ptr || **allowed_iid_ptr != target_iid) {
        return -1;
    }
    AVS_LIST_DELETE(allowed_iid_ptr);
    return 0;
}

typedef struct {
    anjay_acl_ref_validation_ctx_t validation_ctx;
    AVS_LIST(anjay_iid_t) iids_to_remove;
} enumerate_instances_to_remove_args_t;

/**
 * Puts IIDs for Accces Control instances that do not refer to any valid object
 * instance onto the args->iids_to_remove list.
 */
static int
enumerate_instances_to_remove_clb(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj,
                                  anjay_iid_t iid,
                                  void *args_) {
    (void) obj;
    enumerate_instances_to_remove_args_t *args =
            (enumerate_instances_to_remove_args_t *) args_;
    anjay_oid_t target_oid;
    anjay_iid_t target_iid;
    int result = read_ids_from_ac_instance(anjay, iid, &target_oid, &target_iid,
                                           NULL);
    if (!result
            && _anjay_acl_ref_validate_inst_ref(anjay, &args->validation_ctx,
                                                target_oid, target_iid)) {
        if (!AVS_LIST_INSERT_NEW(anjay_iid_t, &args->iids_to_remove)) {
            anjay_log(ERROR, _("out of memory"));
            return -1;
        }
        *args->iids_to_remove = iid;
    }
    return result;
}

/**
 * Removes Access Control instances that do not refer to any valid object
 * instance.
 */
static int perform_removes(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *ac_obj,
                           anjay_notify_queue_t *new_notifications_queue) {
    enumerate_instances_to_remove_args_t args = {
        .validation_ctx = _anjay_acl_ref_validation_ctx_new(),
        .iids_to_remove = NULL
    };

    int result = _anjay_dm_foreach_instance(
            anjay, ac_obj, enumerate_instances_to_remove_clb, &args);
    _anjay_acl_ref_validation_ctx_cleanup(&args.validation_ctx);
    AVS_LIST_CLEAR(&args.iids_to_remove) {
        (void) (result
                || (result = _anjay_dm_call_instance_remove(
                            anjay, ac_obj, *args.iids_to_remove, NULL))
                || (result = _anjay_notify_queue_instance_removed(
                            new_notifications_queue,
                            ANJAY_DM_OID_ACCESS_CONTROL,
                            *args.iids_to_remove)));
    }
    return result;
}

typedef struct {
    bool oid_found;
    bool oiid_found;
    bool acl_found;
    bool owner_found;
} validate_resources_to_write_clb_args_t;

static int
validate_resources_to_write_clb(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_dm_resource_kind_t kind,
                                anjay_dm_resource_presence_t presence,
                                void *args_) {
    (void) anjay;
    (void) obj;
    (void) iid;
    (void) presence;
    validate_resources_to_write_clb_args_t *args =
            (validate_resources_to_write_clb_args_t *) args_;
    // we act as part of the bootstrap process,
    // so we don't check the writable flag
    switch (rid) {
    case ANJAY_DM_RID_ACCESS_CONTROL_OID:
        if (!args->oid_found && _anjay_dm_res_kind_single_readable(kind)) {
            args->oid_found = true;
            return 0;
        }
        return -1;
    case ANJAY_DM_RID_ACCESS_CONTROL_OIID:
        if (!args->oiid_found && _anjay_dm_res_kind_single_readable(kind)) {
            args->oiid_found = true;
            return 0;
        }
        return -1;
    case ANJAY_DM_RID_ACCESS_CONTROL_ACL:
        if (!args->acl_found && _anjay_dm_res_kind_multiple(kind)) {
            args->acl_found = true;
            return 0;
        }
        return -1;
    case ANJAY_DM_RID_ACCESS_CONTROL_OWNER:
        if (!args->owner_found && _anjay_dm_res_kind_single_readable(kind)) {
            args->owner_found = true;
            return 0;
        }
        return -1;
    default:
        return 0;
    }
}

static int
validate_resources_to_write(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *ac_obj,
                            anjay_iid_t ac_iid) {
    validate_resources_to_write_clb_args_t args = {
        .oid_found = false,
        .oiid_found = false,
        .acl_found = false,
        .owner_found = false
    };
    int result =
            _anjay_dm_foreach_resource(anjay, ac_obj, ac_iid,
                                       validate_resources_to_write_clb, &args);
    if (!result
            && !(args.oid_found && args.oiid_found && args.acl_found
                 && args.owner_found)) {
        result = -1;
    }
    return result;
}

/**
 * Creates Access Control object instances for objects instances listed in
 * known_added_iids entries inside incoming_queue.
 */
static int perform_adds(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *ac_obj,
                        anjay_notify_queue_t notifications_already_queued,
                        anjay_notify_queue_t *new_notifications_queue) {
    const anjay_ssid_t origin_ssid = _anjay_dm_current_ssid(anjay);

    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, notifications_already_queued) {
        if (it->oid == ANJAY_DM_OID_SECURITY
                || it->oid == ANJAY_DM_OID_ACCESS_CONTROL) {
            continue;
        }

        // create Access Control object instances for created instances
        AVS_LIST(anjay_iid_t) iid_it;
        AVS_LIST_FOREACH(iid_it, it->instance_set_changes.known_added_iids) {
            int result = find_ac_instance_by_target(anjay, ac_obj, NULL,
                                                    it->oid, *iid_it);
            if (!result) {
                // AC instance already exists, skip
                continue;
            }
            anjay_iid_t ac_iid;
            if (result != ANJAY_ERR_NOT_FOUND
                    || (result = _anjay_dm_select_free_iid(anjay, ac_obj,
                                                           &ac_iid))
                    || (result = _anjay_dm_call_instance_create(anjay, ac_obj,
                                                                ac_iid, NULL))
                    || (result = validate_resources_to_write(anjay, ac_obj,
                                                             ac_iid))
                    || (result = write_u16(anjay, ac_obj, ac_iid,
                                           ANJAY_DM_RID_ACCESS_CONTROL_OID,
                                           ANJAY_ID_INVALID, it->oid))
                    || (result = write_u16(anjay, ac_obj, ac_iid,
                                           ANJAY_DM_RID_ACCESS_CONTROL_OIID,
                                           ANJAY_ID_INVALID, *iid_it))
                    || (result = write_u16(anjay, ac_obj, ac_iid,
                                           ANJAY_DM_RID_ACCESS_CONTROL_ACL,
                                           origin_ssid,
                                           ANJAY_ACCESS_MASK_FULL
                                                   & ~ANJAY_ACCESS_MASK_CREATE))
                    || (result = write_u16(anjay, ac_obj, ac_iid,
                                           ANJAY_DM_RID_ACCESS_CONTROL_OWNER,
                                           ANJAY_ID_INVALID, origin_ssid))
                    || (result = _anjay_notify_queue_instance_created(
                                new_notifications_queue,
                                ANJAY_DM_OID_ACCESS_CONTROL, ac_iid))) {
                return result;
            }
        }
    }
    return 0;
}

static const anjay_notify_queue_object_entry_t *
get_ac_notif_entry(anjay_notify_queue_t queue) {
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        // Queue entries are sorted by OID, compare with
        // find_or_create_object_entry() in notify.c
        if (it->oid >= ANJAY_DM_OID_ACCESS_CONTROL) {
            break;
        }
    }
    if (it && it->oid == ANJAY_DM_OID_ACCESS_CONTROL) {
        return it;
    }
    return NULL;
}

static int generate_apparent_instance_set_change_notifications(
        anjay_t *anjay,
        anjay_notify_queue_t notifications_already_queued,
        anjay_notify_queue_t *new_notifications_queue) {
    const anjay_notify_queue_object_entry_t *ac_notif =
            get_ac_notif_entry(notifications_already_queued);
    if (!ac_notif) {
        return 0;
    }

    anjay_iid_t last_iid = ANJAY_ID_INVALID;
    AVS_LIST(anjay_notify_queue_resource_entry_t) it;
    AVS_LIST_FOREACH(it, ac_notif->resources_changed) {
        // Resource entries are sorted lexicographically over (IID, RID) pairs,
        // compare with _anjay_notify_queue_resource_change() in notify.c
        if (it->iid == last_iid) {
            continue;
        }

        last_iid = it->iid;
        anjay_oid_t target_oid;
        int result;
        if ((result = read_ids_from_ac_instance(anjay, it->iid, &target_oid,
                                                NULL, NULL))
                || (result = _anjay_notify_queue_instance_set_unknown_change(
                            new_notifications_queue, target_oid))) {
            return result;
        }
    }
    return 0;
}

#endif // ANJAY_WITH_ACCESS_CONTROL

int _anjay_sync_access_control(
        anjay_t *anjay, anjay_notify_queue_t notifications_already_queued) {
#ifndef ANJAY_WITH_ACCESS_CONTROL
    (void) anjay;
    (void) notifications_already_queued;
    return 0;
#else  // ANJAY_WITH_ACCESS_CONTROL
    if (anjay->access_control_sync_in_progress) {
        return 0;
    }

    const anjay_dm_object_def_t *const *ac_obj = get_access_control(anjay);
    if (!ac_obj) {
        return 0;
    }
    bool might_caused_orphaned_ac_instances;
    bool have_adds;
    bool might_have_removes;
    what_changed(_anjay_dm_current_ssid(anjay), notifications_already_queued,
                 &might_caused_orphaned_ac_instances, &have_adds,
                 &might_have_removes);
    int result = 0;
    anjay->access_control_sync_in_progress = true;
    _anjay_dm_transaction_begin(anjay);
    anjay_notify_queue_t new_notifications_queue = NULL;
    if (might_have_removes) {
        result = perform_removes(anjay, ac_obj, &new_notifications_queue);
    }
    if (!result && might_caused_orphaned_ac_instances) {
        result = remove_orphaned_instances(anjay, ac_obj,
                                           &new_notifications_queue);
    }
    if (!result && have_adds) {
        result = perform_adds(anjay, ac_obj, notifications_already_queued,
                              &new_notifications_queue);
    }
    if (!result) {
        result = generate_apparent_instance_set_change_notifications(
                anjay, notifications_already_queued, &new_notifications_queue);
    }
    if (!result) {
        result = _anjay_notify_flush(anjay, &new_notifications_queue);
    }
    _anjay_notify_clear_queue(&new_notifications_queue);
    result = _anjay_dm_transaction_finish(anjay, result);
    anjay->access_control_sync_in_progress = false;
    return result;
#endif // ANJAY_WITH_ACCESS_CONTROL
}
