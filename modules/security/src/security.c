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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "security.h"
#include "transaction.h"
#include "utils.h"

VISIBILITY_SOURCE_BEGIN

sec_repr_t *_anjay_sec_get(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return AVS_CONTAINER_OF(obj_ptr, sec_repr_t, def);
}

static inline sec_instance_t *
find_instance(sec_repr_t *repr, anjay_iid_t iid) {
    if (!repr) {
        return NULL;
    }
    AVS_LIST(sec_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (it->iid == iid) {
            return it;
        } else if (it->iid > iid) {
            break;
        }
    }
    return NULL;
}

static anjay_iid_t get_new_iid(AVS_LIST(sec_instance_t) instances) {
    anjay_iid_t iid = 0;
    AVS_LIST(sec_instance_t) it;
    AVS_LIST_FOREACH(it, instances) {
        if (it->iid == iid) {
            ++iid;
        } else if (it->iid > iid) {
            break;
        }
    }
    return iid;
}

static int assign_iid(sec_repr_t *repr, anjay_iid_t *inout_iid) {
    *inout_iid = get_new_iid(repr->instances);
    if (*inout_iid == ANJAY_IID_INVALID) {
        return -1;
    }
    return 0;
}

static int add_instance(sec_repr_t *repr,
                        const anjay_security_instance_t *instance,
                        anjay_iid_t *inout_iid) {
    if (*inout_iid == ANJAY_IID_INVALID) {
        if (assign_iid(repr, inout_iid)) {
            return -1;
        }
    } else if (find_instance(repr, *inout_iid)) {
        return -1;
    }
    AVS_LIST(sec_instance_t) new_instance = AVS_LIST_NEW_ELEMENT(sec_instance_t);
    if (!new_instance) {
        security_log(ERROR, "Out of memory");
        return -1;
    }
    new_instance->iid = *inout_iid;
    new_instance->server_uri = strdup(instance->server_uri);
    if (!new_instance->server_uri) {
        goto error;
    }
    new_instance->is_bootstrap = instance->bootstrap_server;
    new_instance->security_mode = instance->security_mode;
    new_instance->holdoff_s = instance->client_holdoff_s;
    new_instance->bs_timeout_s = instance->bootstrap_timeout_s;
    if (_anjay_raw_buffer_clone(
                &new_instance->public_cert_or_psk_identity,
                &(const anjay_raw_buffer_t){
                        .data = (void *) (intptr_t)
                                        instance->public_cert_or_psk_identity,
                        .size = instance->public_cert_or_psk_identity_size })) {
        goto error;
    }
    if (_anjay_raw_buffer_clone(
                &new_instance->private_cert_or_psk_key,
                &(const anjay_raw_buffer_t){
                        .data = (void *) (intptr_t)
                                        instance->private_cert_or_psk_key,
                        .size = instance->private_cert_or_psk_key_size })) {
        goto error;
    }
    if (_anjay_raw_buffer_clone(
                &new_instance->server_public_key,
                &(const anjay_raw_buffer_t){
                        .data = (void *) (intptr_t) instance->server_public_key,
                        .size = instance->server_public_key_size })) {
        goto error;
    }
    new_instance->has_is_bootstrap = true;
    new_instance->has_security_mode = true;

    if (new_instance->is_bootstrap) {
        new_instance->has_ssid = false;
    } else {
        new_instance->ssid = instance->ssid;
        new_instance->has_ssid = true;
    }

    AVS_LIST(sec_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &repr->instances) {
        if ((*ptr)->iid > new_instance->iid) {
            break;
        }
    }
    AVS_LIST_INSERT(ptr, new_instance);
    return 0;

error:
    _anjay_sec_destroy_instances(&new_instance);
    return -1;
}

static int del_instance(sec_repr_t *repr, anjay_iid_t iid) {
    AVS_LIST(sec_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST(sec_instance_t) element = AVS_LIST_DETACH(it);
            _anjay_sec_destroy_instances(&element);
            return 0;
        }
    }
    return ANJAY_ERR_NOT_FOUND;
}

static int sec_resource_supported(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_rid_t rid) {
    (void) anjay;
    (void) obj_ptr;

    switch ((security_resource_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
    case SEC_RES_BOOTSTRAP_SERVER:
    case SEC_RES_UDP_SECURITY_MODE:
    case SEC_RES_PK_OR_IDENTITY:
    case SEC_RES_SERVER_PK:
    case SEC_RES_SECRET_KEY:
    case SEC_RES_SHORT_SERVER_ID:
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        return 1;
    default:
        return 0;
    }
}

static int sec_resource_operations(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_rid_t rid,
                                   anjay_dm_resource_op_mask_t *out) {
    (void) anjay;
    (void) obj_ptr;
    (void) rid;
    *out = ANJAY_DM_RESOURCE_OP_NONE;
    return 0;
}

static inline int sec_resource_present(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t iid,
                                       anjay_rid_t rid) {
    (void) anjay;
    (void) iid;
    const sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    if (rid == SEC_RES_SHORT_SERVER_ID) {
        return inst->has_ssid;
    } else if (rid == SEC_RES_CLIENT_HOLD_OFF_TIME) {
        return inst->holdoff_s >= 0;
    } else if (rid == SEC_RES_BOOTSTRAP_TIMEOUT) {
        return inst->bs_timeout_s >= 0;
    }
    return 1;
}

static int sec_read(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_output_ctx_t *ctx) {
    (void) anjay;

    const sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch ((security_resource_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
        return anjay_ret_string(ctx, inst->server_uri);
    case SEC_RES_BOOTSTRAP_SERVER:
        return anjay_ret_bool(ctx, inst->is_bootstrap);
    case SEC_RES_UDP_SECURITY_MODE:
        return anjay_ret_i32(ctx, (int32_t) inst->security_mode);
    case SEC_RES_SERVER_PK:
        return anjay_ret_bytes(ctx, inst->server_public_key.data,
                               inst->server_public_key.size);
    case SEC_RES_PK_OR_IDENTITY:
        return anjay_ret_bytes(ctx, inst->public_cert_or_psk_identity.data,
                               inst->public_cert_or_psk_identity.size);
    case SEC_RES_SECRET_KEY:
        return anjay_ret_bytes(ctx, inst->private_cert_or_psk_key.data,
                               inst->private_cert_or_psk_key.size);
    case SEC_RES_SHORT_SERVER_ID:
        return anjay_ret_i32(ctx, (int32_t) inst->ssid);
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
        return anjay_ret_i32(ctx, inst->holdoff_s);
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        return anjay_ret_i32(ctx, inst->bs_timeout_s);
    default:
        security_log(ERROR, "not implemented: get /0/%u/%u", iid, rid);
        return ANJAY_ERR_NOT_IMPLEMENTED;
    }
}

static int sec_write(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_input_ctx_t *ctx) {
    (void) anjay;
    sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    int retval;
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch ((security_resource_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
        return _anjay_sec_fetch_string(ctx, &inst->server_uri);
    case SEC_RES_BOOTSTRAP_SERVER:
        if (!(retval = anjay_get_bool(ctx, &inst->is_bootstrap))) {
            inst->has_is_bootstrap = true;
        }
        return retval;
    case SEC_RES_UDP_SECURITY_MODE:
        if (!(retval = _anjay_sec_fetch_security_mode(ctx,
                                                      &inst->security_mode))) {
            inst->has_security_mode = true;
        }
        return retval;
    case SEC_RES_PK_OR_IDENTITY:
        return _anjay_sec_fetch_bytes(ctx, &inst->public_cert_or_psk_identity);
    case SEC_RES_SERVER_PK:
        return _anjay_sec_fetch_bytes(ctx, &inst->server_public_key);
    case SEC_RES_SECRET_KEY:
        return _anjay_sec_fetch_bytes(ctx, &inst->private_cert_or_psk_key);
    case SEC_RES_SHORT_SERVER_ID:
        if (!(retval = _anjay_sec_fetch_short_server_id(ctx, &inst->ssid))) {
            inst->has_ssid = true;
        }
        return retval;
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
        return anjay_get_i32(ctx, &inst->holdoff_s);
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        return anjay_get_i32(ctx, &inst->bs_timeout_s);
    case SEC_RES_SMS_SECURITY_MODE:
    case SEC_RES_SMS_BINDING_KEY_PARAMS:
    case SEC_RES_SMS_BINDING_SECRET_KEYS:
    case SEC_RES_SERVER_SMS_NUMBER:
        security_log(ERROR, "not implemented: write /0/%u/%u", iid, rid);
        return ANJAY_ERR_NOT_IMPLEMENTED;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int sec_instance_it(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t *out,
                           void **cookie) {
    (void) anjay;

    sec_repr_t *repr = _anjay_sec_get(obj_ptr);
    AVS_LIST(sec_instance_t) curr = (AVS_LIST(sec_instance_t)) *cookie;

    if (!curr) {
        curr = repr->instances;
    } else {
        curr = AVS_LIST_NEXT(curr);
    }

    *out = curr ? curr->iid : ANJAY_IID_INVALID;
    *cookie = curr;
    return 0;
}

static int sec_instance_present(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay;
    return find_instance(_anjay_sec_get(obj_ptr), iid) != NULL;
}

static int sec_instance_create(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t *inout_iid,
                               anjay_ssid_t ssid) {
    (void) anjay; (void) ssid;
    sec_repr_t *repr = _anjay_sec_get(obj_ptr);
    if (*inout_iid == ANJAY_IID_INVALID && assign_iid(repr, inout_iid)) {
        return ANJAY_ERR_INTERNAL;
    }

    AVS_LIST(sec_instance_t) created = AVS_LIST_NEW_ELEMENT(sec_instance_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }

    created->iid = *inout_iid;
    created->ssid = *inout_iid;

    AVS_LIST(sec_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &repr->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return 0;
}

static int sec_instance_remove(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    return del_instance(_anjay_sec_get(obj_ptr), iid);
}

static int
sec_transaction_begin(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_begin_impl(_anjay_sec_get(obj_ptr));
}

static int
sec_transaction_commit(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_commit_impl(_anjay_sec_get(obj_ptr));
}

static int
sec_transaction_validate(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_validate_impl(_anjay_sec_get(obj_ptr));
}

static int
sec_transaction_rollback(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_rollback_impl(_anjay_sec_get(obj_ptr));
}

static int
sec_instance_reset(anjay_t *anjay,
                   const anjay_dm_object_def_t *const *obj_ptr,
                   anjay_iid_t iid) {
    (void) anjay;
    sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    _anjay_sec_destroy_instance_fields(inst);
    memset(inst, 0, sizeof(sec_instance_t));
    inst->iid = iid;
    return 0;
}

static const anjay_dm_object_def_t SECURITY = {
    .oid = 0,
    .rid_bound = _SEC_RID_BOUND,
    .instance_it = sec_instance_it,
    .instance_present = sec_instance_present,
    .instance_create = sec_instance_create,
    .instance_remove = sec_instance_remove,
    .instance_reset = sec_instance_reset,
    .resource_present = sec_resource_present,
    .resource_supported = sec_resource_supported,
    .resource_operations = sec_resource_operations,
    .resource_read = sec_read,
    .resource_write = sec_write,
    .transaction_begin = sec_transaction_begin,
    .transaction_commit = sec_transaction_commit,
    .transaction_validate = sec_transaction_validate,
    .transaction_rollback = sec_transaction_rollback
};

const anjay_dm_object_def_t **anjay_security_object_create(void) {
    sec_repr_t *repr = (sec_repr_t *) calloc(1, sizeof(sec_repr_t));
    if (!repr) {
        return NULL;
    }
    repr->def = &SECURITY;
    return &repr->def;
}

int anjay_security_object_add_instance(
        const anjay_dm_object_def_t *const *obj_ptr,
        const anjay_security_instance_t *instance,
        anjay_iid_t *inout_iid) {
    sec_repr_t *repr = _anjay_sec_get(obj_ptr);
    int retval = add_instance(repr, instance, inout_iid);
    if (!retval && (retval = _anjay_sec_object_validate(repr))) {
        (void) del_instance(repr, *inout_iid);
    }
    return retval;
}

void anjay_security_object_purge(const anjay_dm_object_def_t *const *obj_ptr) {
    sec_repr_t *sec = _anjay_sec_get(obj_ptr);
    _anjay_sec_destroy_instances(&sec->instances);
    _anjay_sec_destroy_instances(&sec->saved_instances);
}

void anjay_security_object_delete(const anjay_dm_object_def_t **def) {
    anjay_security_object_purge(def);
    free(_anjay_sec_get(def));
}

#ifdef ANJAY_TEST
#include "test/api.c"
#endif
