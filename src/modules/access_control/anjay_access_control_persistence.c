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

#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
#        include <avsystem/commons/avs_persistence.h>
#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE

#    include <anjay/access_control.h>

#    include "anjay_mod_access_control.h"

#    include <string.h>

VISIBILITY_SOURCE_BEGIN

#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE

static avs_error_t handle_acl_entry(avs_persistence_context_t *ctx,
                                    void *element_,
                                    void *user_data) {
    (void) user_data;
    acl_entry_t *element = (acl_entry_t *) element_;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &element->mask)))
            || avs_is_err((err = avs_persistence_u16(ctx, &element->ssid))));
    return err;
}

static void cleanup_acl_entry(void *element) {
    // no resources allocated in acl_entry_t
    (void) element;
}

static avs_error_t handle_acl(avs_persistence_context_t *ctx,
                              access_control_instance_t *inst) {
    avs_error_t err = avs_persistence_bool(ctx, &inst->has_acl);
    if (avs_is_ok(err) && inst->has_acl) {
        err = avs_persistence_list(ctx, (AVS_LIST(void) *) &inst->acl,
                                   sizeof(*inst->acl), handle_acl_entry, NULL,
                                   cleanup_acl_entry);
    }
    return err;
}

static avs_error_t persist_instance(avs_persistence_context_t *ctx,
                                    void *element_,
                                    void *user_data) {
    (void) user_data;
    access_control_instance_t *element = (access_control_instance_t *) element_;
    anjay_iid_t target_iid = (anjay_iid_t) element->target.iid;
    if (target_iid != element->target.iid) {
        return avs_errno(AVS_EINVAL);
    }
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &element->target.oid)))
            || avs_is_err((err = avs_persistence_u16(ctx, &element->iid)))
            || avs_is_err((err = avs_persistence_u16(ctx, &target_iid)))
            || avs_is_err((err = avs_persistence_u16(ctx, &element->owner)))
            || avs_is_err((err = handle_acl(ctx, element))));
    return err;
}

static bool is_object_registered(anjay_t *anjay, anjay_oid_t oid) {
    return oid != ANJAY_DM_OID_SECURITY
           && _anjay_dm_find_object_by_oid(anjay, oid) != NULL;
}

static avs_error_t restore_instance(access_control_instance_t *out_instance,
                                    avs_persistence_context_t *ctx) {
    anjay_iid_t target_iid = 0;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &out_instance->iid)))
            || avs_is_err((err = avs_persistence_u16(ctx, &target_iid)))
            || avs_is_err(
                       (err = avs_persistence_u16(ctx, &out_instance->owner)))
            || avs_is_err((err = handle_acl(ctx, out_instance))));
    if (avs_is_ok(err)) {
        out_instance->target.iid = target_iid;
    }
    return err;
}

static avs_error_t
restore_instances(anjay_t *anjay,
                  AVS_LIST(access_control_instance_t) *instances_ptr,
                  avs_persistence_context_t *restore_ctx) {
    uint32_t count;
    avs_error_t err = avs_persistence_u32(restore_ctx, &count);
    if (avs_is_err(err)) {
        return err;
    }
    if (count > UINT16_MAX) {
        return avs_errno(AVS_EBADMSG);
    }
    AVS_LIST(access_control_instance_t) *tail = instances_ptr;
    while (count--) {
        access_control_instance_t instance;
        memset(&instance, 0, sizeof(instance));
        if (avs_is_err((err = avs_persistence_u16(restore_ctx,
                                                  &instance.target.oid)))
                || avs_is_err(
                           (err = restore_instance(&instance, restore_ctx)))) {
            return err;
        }

        if (!is_object_registered(anjay, instance.target.oid)) {
            AVS_LIST_CLEAR(&instance.acl);
        } else {
            AVS_LIST(access_control_instance_t) entry =
                    AVS_LIST_NEW_ELEMENT(access_control_instance_t);
            if (!entry) {
                ac_log(ERROR, _("out of memory"));
                return avs_errno(AVS_ENOMEM);
            }
            *entry = instance;
            AVS_LIST_INSERT(tail, entry);
            AVS_LIST_ADVANCE_PTR(&tail);
        }
    }
    return AVS_OK;
}

static avs_error_t
restore(anjay_t *anjay, access_control_t *ac, avs_stream_t *in) {
    avs_persistence_context_t restore_ctx =
            avs_persistence_restore_context_create(in);
    access_control_state_t state = { NULL, false };
    avs_error_t err = restore_instances(anjay, &state.instances, &restore_ctx);
    if (avs_is_err(err)) {
        _anjay_access_control_clear_state(&state);
        return err;
    }
    _anjay_access_control_clear_state(&ac->current);
    ac->current = state;
    ac->last_accessed_instance = NULL;
    return AVS_OK;
}

static const char MAGIC[] = { 'A', 'C', 'O', '\1' };

avs_error_t anjay_access_control_persist(anjay_t *anjay, avs_stream_t *out) {
    access_control_t *ac = _anjay_access_control_get(anjay);
    if (!ac) {
        ac_log(ERROR, _("Access Control not installed in this Anjay object"));
        return avs_errno(AVS_EBADF);
    }

    avs_error_t err = avs_stream_write(out, MAGIC, sizeof(MAGIC));
    if (avs_is_err(err)) {
        return err;
    }
    avs_persistence_context_t ctx = avs_persistence_store_context_create(out);
    AVS_LIST(access_control_instance_t) *list_ptr =
            ac->in_transaction ? &ac->saved_state.instances
                               : &ac->current.instances;
    err = avs_persistence_list(&ctx, (AVS_LIST(void) *) list_ptr,
                               sizeof(**list_ptr), persist_instance, NULL,
                               NULL);
    if (avs_is_ok(err)) {
        ac_log(INFO, _("Access Control state persisted"));
        _anjay_access_control_clear_modified(ac);
    }
    return err;
}

avs_error_t anjay_access_control_restore(anjay_t *anjay, avs_stream_t *in) {
    access_control_t *ac = _anjay_access_control_get(anjay);
    if (!ac) {
        ac_log(ERROR, _("Access Control not installed in this Anjay object"));
        return avs_errno(AVS_EBADF);
    }

    if (ac->in_transaction) {
        ac_log(ERROR, _("Cannot restore Access Control state while the object "
                        "is in transaction"));
        return avs_errno(AVS_EBADF);
    }

    char magic_header[sizeof(MAGIC)];
    avs_error_t err =
            avs_stream_read_reliably(in, magic_header, sizeof(magic_header));
    if (avs_is_err(err)) {
        ac_log(WARNING, _("magic constant not found"));
        return err;
    }

    if (memcmp(magic_header, MAGIC, sizeof(MAGIC))) {
        ac_log(WARNING, _("header magic constant mismatch"));
        return avs_errno(AVS_EBADMSG);
    }
    if (avs_is_ok((err = restore(anjay, ac, in)))) {
        _anjay_access_control_clear_modified(ac);
        ac_log(INFO, _("Access Control state restored"));
    }
    return err;
}

#        ifdef ANJAY_TEST
#            include "tests/modules/access_control/persistence.c"
#        endif // ANJAY_TEST

#    else // AVS_COMMONS_WITH_AVS_PERSISTENCE

avs_error_t anjay_access_control_persist(anjay_t *anjay, avs_stream_t *out) {
    (void) anjay;
    (void) out;
    ac_log(ERROR, _("Persistence not compiled in"));
    return avs_errno(AVS_ENOTSUP);
}

avs_error_t anjay_access_control_restore(anjay_t *anjay, avs_stream_t *in) {
    (void) anjay;
    (void) in;
    ac_log(ERROR, _("Persistence not compiled in"));
    return avs_errno(AVS_ENOTSUP);
}

#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE

#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
