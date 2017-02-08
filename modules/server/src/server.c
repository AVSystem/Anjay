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

#include <string.h>

#include "server.h"
#include "transaction.h"
#include "utils.h"

VISIBILITY_SOURCE_BEGIN

server_repr_t *_anjay_serv_get(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return AVS_CONTAINER_OF(obj_ptr, server_repr_t, def);
}

static inline server_instance_t *
find_instance(server_repr_t *repr,
              anjay_iid_t iid) {
    if (!repr) {
        return NULL;
    }
    AVS_LIST(server_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (it->iid == iid) {
            return it;
        } else if (it->iid > iid) {
            break;
        }
    }
    return NULL;
}

static anjay_iid_t get_new_iid(AVS_LIST(server_instance_t) instances) {
    anjay_iid_t iid = 0;
    AVS_LIST(server_instance_t) it;
    AVS_LIST_FOREACH(it, instances) {
        if (it->iid == iid) {
            ++iid;
        } else if (it->iid > iid) {
            break;
        }
    }
    return iid;
}

static int assign_iid(server_repr_t *repr, anjay_iid_t *inout_iid) {
    *inout_iid = get_new_iid(repr->instances);
    if (*inout_iid == ANJAY_IID_INVALID) {
        return -1;
    }
    return 0;
}

static int insert_created_instance(server_repr_t *repr,
                                   AVS_LIST(server_instance_t) new_instance) {
    AVS_LIST(server_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &repr->instances) {
        assert((*ptr)->iid != new_instance->iid);
        if ((*ptr)->iid > new_instance->iid) {
            break;
        }
    }
    AVS_LIST_INSERT(ptr, new_instance);
    return 0;
}

static int add_instance(server_repr_t *repr,
                        const anjay_server_instance_t *instance,
                        anjay_iid_t *inout_iid) {
    if (*inout_iid == ANJAY_IID_INVALID) {
        if (assign_iid(repr, inout_iid)) {
            return -1;
        }
    } else if (find_instance(repr, *inout_iid)) {
        return -1;
    }
    AVS_LIST(server_instance_t) new_instance =
            AVS_LIST_NEW_ELEMENT(server_instance_t);
    if (!new_instance) {
        server_log(ERROR, "Out of memory");
        return -1;
    }
    new_instance->data = *instance;
    new_instance->iid = *inout_iid;
    new_instance->has_ssid = true;
    new_instance->has_lifetime = true;
    new_instance->has_binding = true;
    new_instance->has_notification_storing = true;
    if (insert_created_instance(repr, new_instance)) {
        AVS_LIST_CLEAR(&new_instance);
        return -1;
    }
    return 0;
}

static int del_instance(server_repr_t *repr, anjay_iid_t iid) {
    AVS_LIST(server_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST_DELETE(it);
            return 0;
        } else if ((*it)->iid > iid) {
            break;
        }
    }
    return ANJAY_ERR_NOT_FOUND;
}

static int serv_instance_it(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t *out,
                            void **cookie) {
    (void) anjay;

    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    AVS_LIST(server_instance_t) curr = (AVS_LIST(server_instance_t)) *cookie;

    if (!curr) {
        curr = repr->instances;
    } else {
        curr = AVS_LIST_NEXT(curr);
    }

    *out = curr ? curr->iid : ANJAY_IID_INVALID;
    *cookie = curr;
    return 0;
}

static inline void reset_instance_resources(server_instance_t *serv) {
    const anjay_iid_t iid = serv->iid;
    memset(serv, 0, sizeof(*serv));
    serv->data.lifetime = -1;
    serv->data.default_min_period = -1;
    serv->data.default_max_period = -1;
    serv->data.disable_timeout = -1;
    /* This is not a resource, therefore must be restored */
    serv->iid = iid;
}

static int serv_instance_present(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid) {
    (void) anjay;
    return find_instance(_anjay_serv_get(obj_ptr), iid) != NULL;
}

static int serv_instance_create(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t *inout_iid,
                                anjay_ssid_t ssid) {
    (void) anjay; (void) ssid;
    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    if (*inout_iid == ANJAY_IID_INVALID && assign_iid(repr, inout_iid)) {
        server_log(ERROR, "Cannot assign new Instance id");
        return ANJAY_ERR_INTERNAL;
    }
    AVS_LIST(server_instance_t) created =
            AVS_LIST_NEW_ELEMENT(server_instance_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }
    created->iid = *inout_iid;
    reset_instance_resources(created);

    if (insert_created_instance(repr, created)) {
        AVS_LIST_CLEAR(&created);
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

static int serv_instance_remove(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay;
    return del_instance(_anjay_serv_get(obj_ptr), iid);
}

static int serv_instance_reset(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    server_instance_t *inst = find_instance(_anjay_serv_get(obj_ptr), iid);
    anjay_ssid_t ssid = inst->data.ssid;
    reset_instance_resources(inst);
    inst->data.ssid = ssid;
    return 0;
}

static int serv_resource_operations(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_rid_t rid,
                                    anjay_dm_resource_op_mask_t *out) {
    (void) anjay;
    (void) obj_ptr;
    *out = ANJAY_DM_RESOURCE_OP_NONE;

    switch ((server_rid_t) rid) {
    case SERV_RES_SSID:
        *out = ANJAY_DM_RESOURCE_OP_BIT_R;
        break;
    case SERV_RES_LIFETIME:
    case SERV_RES_DEFAULT_MIN_PERIOD:
    case SERV_RES_DEFAULT_MAX_PERIOD:
    case SERV_RES_DISABLE_TIMEOUT:
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
    case SERV_RES_BINDING:
        *out = ANJAY_DM_RESOURCE_OP_BIT_R | ANJAY_DM_RESOURCE_OP_BIT_W;
        break;
    case SERV_RES_DISABLE:
    case SERV_RES_REGISTRATION_UPDATE_TRIGGER:
        *out = ANJAY_DM_RESOURCE_OP_BIT_E;
        break;
    default:
        break;
    }
    return 0;
}

static inline int
serv_resource_present(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj_ptr,
                      anjay_iid_t iid,
                      anjay_rid_t rid) {
    (void) anjay;
    (void) iid;
    const server_instance_t *inst =
            find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);

    switch (rid) {
    case SERV_RES_LIFETIME:
        return inst->has_lifetime;
    case SERV_RES_DISABLE_TIMEOUT:
        return inst->data.disable_timeout >= 0;
    case SERV_RES_DEFAULT_MIN_PERIOD:
        return inst->data.default_min_period >= 0;
    case SERV_RES_DEFAULT_MAX_PERIOD:
        return inst->data.default_max_period >= 0;
    default:
        return 1;
    }
}

static int serv_read(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_output_ctx_t *ctx) {
    (void) anjay;

    const server_instance_t *inst =
            find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);

    switch ((server_rid_t) rid) {
    case SERV_RES_SSID:
        return anjay_ret_i32(ctx, inst->data.ssid);
    case SERV_RES_LIFETIME:
        return anjay_ret_i32(ctx, inst->data.lifetime);
    case SERV_RES_DEFAULT_MIN_PERIOD:
        return anjay_ret_i32(ctx, inst->data.default_min_period);
    case SERV_RES_DEFAULT_MAX_PERIOD:
        return anjay_ret_i32(ctx, inst->data.default_max_period);
    case SERV_RES_DISABLE_TIMEOUT:
        return anjay_ret_i32(ctx, inst->data.disable_timeout);
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        return anjay_ret_bool(ctx, inst->data.notification_storing);
    case SERV_RES_BINDING:
        return anjay_ret_string(ctx,
                                anjay_binding_mode_as_str(inst->data.binding));
    case SERV_RES_DISABLE:
    case SERV_RES_REGISTRATION_UPDATE_TRIGGER:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    default:
        break;
    }
    server_log(ERROR, "invalid enum value: read /1/%u/%u", iid, rid);
    return ANJAY_ERR_NOT_FOUND;
}

static int serv_write(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj_ptr,
                      anjay_iid_t iid,
                      anjay_rid_t rid,
                      anjay_input_ctx_t *ctx) {
    (void) anjay;

    server_instance_t *inst = find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);
    int retval;

    switch ((server_rid_t) rid) {
    case SERV_RES_SSID:
        if (!(retval = _anjay_serv_fetch_ssid(ctx, &inst->data.ssid))) {
            inst->has_ssid = true;
        }
        return retval;
    case SERV_RES_LIFETIME:
        if (!(retval = anjay_get_i32(ctx, &inst->data.lifetime))) {
            inst->has_lifetime = true;
        }
        return retval;
    case SERV_RES_DEFAULT_MIN_PERIOD:
        return _anjay_serv_fetch_validated_i32(ctx, 0, INT32_MAX,
                                               &inst->data.default_min_period);
    case SERV_RES_DEFAULT_MAX_PERIOD:
        return _anjay_serv_fetch_validated_i32(ctx, 1, INT32_MAX,
                                               &inst->data.default_max_period);
    case SERV_RES_DISABLE_TIMEOUT:
        return _anjay_serv_fetch_validated_i32(ctx, 0, INT32_MAX,
                                               &inst->data.disable_timeout);
    case SERV_RES_BINDING:
        if (!(retval = _anjay_serv_fetch_binding(ctx, &inst->data.binding))) {
            inst->has_binding = true;
        }
        return retval;

    case SERV_RES_DISABLE:
    case SERV_RES_REGISTRATION_UPDATE_TRIGGER:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;

    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        if (!(retval = anjay_get_bool(ctx, &inst->data.notification_storing))) {
            inst->has_notification_storing = true;
        }
        return retval;
    default:
        break;
    }
    return ANJAY_ERR_NOT_FOUND;
}

static int serv_execute(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj_ptr,
                        anjay_iid_t iid,
                        anjay_rid_t rid,
                        anjay_execute_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) ctx;
    server_instance_t *inst = find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);

    switch ((server_rid_t)rid) {
    case SERV_RES_DISABLE:
        return anjay_disable_server(anjay, inst->data.ssid);
    case SERV_RES_REGISTRATION_UPDATE_TRIGGER:
        return anjay_schedule_registration_update(anjay, inst->data.ssid);

    case SERV_RES_SSID:
    case SERV_RES_LIFETIME:
    case SERV_RES_DEFAULT_MIN_PERIOD:
    case SERV_RES_DEFAULT_MAX_PERIOD:
    case SERV_RES_DISABLE_TIMEOUT:
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
    case SERV_RES_BINDING:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    default:
        break;
    }

    server_log(ERROR, "not implemented: /1/%u/%u", iid, rid);
    return ANJAY_ERR_NOT_IMPLEMENTED;
}

static int serv_transaction_begin(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_serv_transaction_begin_impl(_anjay_serv_get(obj_ptr));
}

static int
serv_transaction_commit(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_serv_transaction_commit_impl(_anjay_serv_get(obj_ptr));
}

static int
serv_transaction_validate(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_serv_transaction_validate_impl(_anjay_serv_get(obj_ptr));
}

static int
serv_transaction_rollback(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_serv_transaction_rollback_impl(_anjay_serv_get(obj_ptr));
}

static const anjay_dm_object_def_t SERVER = {
    .oid = 1,
    .rid_bound = _SERV_RID_BOUND,
    .instance_it = serv_instance_it,
    .instance_present = serv_instance_present,
    .instance_create = serv_instance_create,
    .instance_remove = serv_instance_remove,
    .instance_reset = serv_instance_reset,
    .resource_present = serv_resource_present,
    .resource_supported = anjay_dm_resource_supported_TRUE,
    .resource_operations = serv_resource_operations,
    .resource_read = serv_read,
    .resource_write = serv_write,
    .resource_execute = serv_execute,
    .transaction_begin = serv_transaction_begin,
    .transaction_validate = serv_transaction_validate,
    .transaction_commit = serv_transaction_commit,
    .transaction_rollback = serv_transaction_rollback
};

const anjay_dm_object_def_t **anjay_server_object_create(void) {
    server_repr_t *repr = (server_repr_t *) calloc(1, sizeof(server_repr_t));
    if (!repr) {
        return NULL;
    }
    repr->def = &SERVER;
    return &repr->def;
}

int anjay_server_object_add_instance(
        const anjay_dm_object_def_t *const *obj_ptr,
        const anjay_server_instance_t *instance,
        anjay_iid_t *inout_iid) {
    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    int retval = add_instance(repr, instance, inout_iid);
    if (!retval && (retval = _anjay_serv_object_validate(repr))) {
        (void) del_instance(repr, *inout_iid);
    }
    return retval;
}

void anjay_server_object_purge(const anjay_dm_object_def_t *const *obj_ptr) {
    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    _anjay_serv_destroy_instances(&repr->instances);
    _anjay_serv_destroy_instances(&repr->saved_instances);
}

void anjay_server_object_delete(const anjay_dm_object_def_t **def) {
    anjay_server_object_purge(def);
    free(_anjay_serv_get(def));
}

#ifdef ANJAY_TEST
#include "test/api.c"
#endif
