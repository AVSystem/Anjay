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

#include <anjay/access_control.h>
#include <anjay/persistence.h>

#include "access_control.h"

#include <string.h>

VISIBILITY_SOURCE_BEGIN

static int handle_acl_entry(anjay_persistence_context_t *ctx, void *element_) {
    acl_entry_t *element = (acl_entry_t *) element_;
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &element->mask))
                || (retval = anjay_persistence_u16(ctx, &element->ssid)));
    return retval;
}

static int handle_acl(anjay_persistence_context_t *ctx,
                      access_control_instance_t *inst) {
    int retval = anjay_persistence_bool(ctx, &inst->has_acl);
    if (!retval && inst->has_acl) {
        retval = anjay_persistence_list(ctx, (AVS_LIST(void) *) &inst->acl,
                                        sizeof(*inst->acl), handle_acl_entry);
    }
    return retval;
}

static int persist_instance(anjay_persistence_context_t *ctx,
                            void *element_) {
    access_control_instance_t *element = (access_control_instance_t *) element_;
    anjay_iid_t target_iid = (anjay_iid_t) element->target.iid;
    if (target_iid != element->target.iid) {
        return -1;
    }
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &element->target.oid))
                || (retval = anjay_persistence_u16(ctx, &element->iid))
                || (retval = anjay_persistence_u16(ctx, &target_iid))
                || (retval = anjay_persistence_u16(ctx, &element->owner))
                || (retval = handle_acl(ctx, element)));
    return retval;
}

static bool is_object_registered(access_control_t *ac, anjay_oid_t oid) {
    return oid != ANJAY_DM_OID_SECURITY
            && _anjay_dm_find_object_by_oid(ac->anjay, oid) != NULL;
}

static int restore_instance(access_control_instance_t *out_instance,
                            anjay_persistence_context_t *ctx) {
    anjay_iid_t target_iid;
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &out_instance->iid))
            || (retval = anjay_persistence_u16(ctx, &target_iid))
            || (retval = anjay_persistence_u16(ctx, &out_instance->owner))
            || (retval = handle_acl(ctx, out_instance)));
    if (!retval) {
        out_instance->target.iid = target_iid;
    }
    return retval;
}

static int restore_instances(access_control_t *ac,
                             AVS_LIST(access_control_instance_t) *instances_ptr,
                             anjay_persistence_context_t *restore_ctx,
                             anjay_persistence_context_t *ignore_ctx) {
    uint32_t count;
    if (anjay_persistence_u32(restore_ctx, &count) || (count > UINT16_MAX)) {
        return -1;
    }
    AVS_LIST(access_control_instance_t) *tail = instances_ptr;
    while (count--) {
        int retval;
        access_control_instance_t instance;
        memset(&instance, 0, sizeof(instance));
        if ((retval = anjay_persistence_u16(restore_ctx,
                                            &instance.target.oid))) {
            return retval;
        }
        if (!is_object_registered(ac, instance.target.oid)) {
            /* Actually ignore this instance. */
            retval = restore_instance(&instance, ignore_ctx);
        } else {
            retval = restore_instance(&instance, restore_ctx);
        }

        if (retval) {
            return retval;
        }

        AVS_LIST(access_control_instance_t) entry =
                AVS_LIST_NEW_ELEMENT(access_control_instance_t);
        if (!entry) {
            ac_log(ERROR, "out of memory");
            return -1;
        }
        *entry = instance;
        AVS_LIST_INSERT(tail, entry);
        tail = AVS_LIST_NEXT_PTR(tail);
    }
    return 0;
}

static int restore(access_control_t *ac,
                   avs_stream_abstract_t *in) {
    anjay_persistence_context_t *restore_ctx =
            anjay_persistence_restore_context_new(in);
    anjay_persistence_context_t *ignore_ctx =
            anjay_persistence_ignore_context_new(in);
    int retval = -1;
    if (!restore_ctx || !ignore_ctx) {
        goto finish;
    }

    access_control_state_t state = { NULL };
    if ((retval = restore_instances(ac, &state.instances,
                                    restore_ctx, ignore_ctx))) {
        _anjay_access_control_clear_state(&state);
        goto finish;
    }
    _anjay_access_control_clear_state(&ac->current);
    ac->current = state;
finish:
    anjay_persistence_context_delete(restore_ctx);
    anjay_persistence_context_delete(ignore_ctx);
    return retval;
}

static const char MAGIC[] = { 'A', 'C', 'O', '\1' };

int anjay_access_control_persist(const anjay_dm_object_def_t *const *ac_obj,
                                 avs_stream_abstract_t *out) {
    access_control_t *ac = _anjay_access_control_get(ac_obj);
    if (!ac) {
        return -1;
    }
    int retval = avs_stream_write(out, MAGIC, sizeof(MAGIC));
    if (retval) {
        return retval;
    }
    anjay_persistence_context_t *ctx = anjay_persistence_store_context_new(out);
    if (!ctx) {
        ac_log(ERROR, "Out of memory");
        return -1;
    }
    retval = anjay_persistence_list(ctx,
                                    (AVS_LIST(void) *) &ac->current.instances,
                                    sizeof(*ac->current.instances),
                                    persist_instance);
    anjay_persistence_context_delete(ctx);
    return retval;
}

int anjay_access_control_restore(const anjay_dm_object_def_t *const *ac_obj,
                                 avs_stream_abstract_t *in) {
    access_control_t *ac = _anjay_access_control_get(ac_obj);
    if (!ac) {
        return -1;
    }
    char magic_header[sizeof(MAGIC)];
    int retval = avs_stream_read_reliably(in, magic_header, sizeof(magic_header));
    if (retval) {
        ac_log(ERROR, "magic constant not found");
        return retval;
    }

    if (memcmp(magic_header, MAGIC, sizeof(MAGIC))) {
        ac_log(ERROR, "header magic constant mismatch");
        return -1;
    }
    return restore(ac, in);
}

#ifdef ANJAY_TEST
#include "test/persistence.c"
#endif // ANJAY_TEST
