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

#include <string.h>

#include <avsystem/commons/utils.h>

#include <anjay_modules/dm_utils.h>

#include "mod_server.h"
#include "server_transaction.h"
#include "server_utils.h"

VISIBILITY_SOURCE_BEGIN

static inline server_instance_t *find_instance(server_repr_t *repr,
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
    _anjay_serv_mark_modified(repr);
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
    if (instance->binding) {
        if (!anjay_binding_mode_valid(instance->binding)
                || avs_simple_snprintf(new_instance->binding_buf,
                                       sizeof(new_instance->binding_buf), "%s",
                                       instance->binding)
                               < 0) {
            server_log(ERROR, "Unsupported binding mode: %s",
                       instance->binding);
            return -1;
        }
        new_instance->data.binding = new_instance->binding_buf;
    }
    new_instance->iid = *inout_iid;
    new_instance->has_ssid = true;
    new_instance->has_lifetime = true;
    new_instance->has_notification_storing = true;
    if (insert_created_instance(repr, new_instance)) {
        AVS_LIST_CLEAR(&new_instance);
        return -1;
    }
    server_log(INFO, "Added instance %u (SSID: %u)", *inout_iid,
               instance->ssid);
    return 0;
}

static int del_instance(server_repr_t *repr, anjay_iid_t iid) {
    AVS_LIST(server_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST_DELETE(it);
            _anjay_serv_mark_modified(repr);
            return 0;
        } else if ((*it)->iid > iid) {
            break;
        }
    }

    assert(0);
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

    *out = (anjay_iid_t) (curr ? curr->iid : ANJAY_IID_INVALID);
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
    (void) anjay;
    (void) ssid;
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
    assert(inst);

    bool has_ssid = inst->has_ssid;
    anjay_ssid_t ssid = inst->data.ssid;
    reset_instance_resources(inst);
    inst->has_ssid = has_ssid;
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
        if (!inst->data.binding) {
            return ANJAY_ERR_NOT_FOUND;
        }
        return anjay_ret_string(ctx, inst->data.binding);
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

    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    server_instance_t *inst = find_instance(repr, iid);
    assert(inst);
    int retval;

    _anjay_serv_mark_modified(repr);

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
        if (!(retval = _anjay_serv_fetch_binding(ctx, &inst->binding_buf))) {
            inst->data.binding = inst->binding_buf;
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
    (void) anjay;
    (void) obj_ptr;
    (void) ctx;
    server_instance_t *inst = find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);

    switch ((server_rid_t) rid) {
    case SERV_RES_DISABLE: {
        avs_time_duration_t disable_timeout =
                inst->data.disable_timeout < 0
                        ? AVS_TIME_DURATION_INVALID
                        : avs_time_duration_from_scalar(
                                  inst->data.disable_timeout, AVS_TIME_S);

        return anjay_disable_server_with_timeout(anjay, inst->data.ssid,
                                                 disable_timeout);
    }
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
    .oid = ANJAY_DM_OID_SERVER,
    .supported_rids = ANJAY_DM_SUPPORTED_RIDS(
            SERV_RES_SSID,
            SERV_RES_LIFETIME,
            SERV_RES_DEFAULT_MIN_PERIOD,
            SERV_RES_DEFAULT_MAX_PERIOD,
            SERV_RES_DISABLE,
            SERV_RES_DISABLE_TIMEOUT,
            SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            SERV_RES_BINDING,
            SERV_RES_REGISTRATION_UPDATE_TRIGGER),
    .handlers = {
        .instance_it = serv_instance_it,
        .instance_present = serv_instance_present,
        .instance_create = serv_instance_create,
        .instance_remove = serv_instance_remove,
        .instance_reset = serv_instance_reset,
        .resource_present = serv_resource_present,
        .resource_operations = serv_resource_operations,
        .resource_read = serv_read,
        .resource_write = serv_write,
        .resource_execute = serv_execute,
        .transaction_begin = serv_transaction_begin,
        .transaction_validate = serv_transaction_validate,
        .transaction_commit = serv_transaction_commit,
        .transaction_rollback = serv_transaction_rollback
    }
};

server_repr_t *_anjay_serv_get(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr && *obj_ptr == &SERVER);
    return AVS_CONTAINER_OF(obj_ptr, server_repr_t, def);
}

int anjay_server_object_add_instance(anjay_t *anjay,
                                     const anjay_server_instance_t *instance,
                                     anjay_iid_t *inout_iid) {
    assert(anjay);

    const anjay_dm_object_def_t *const *obj_ptr =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    server_repr_t *repr = _anjay_serv_get(obj_ptr);

    const bool modified_since_persist = repr->modified_since_persist;
    int retval = add_instance(repr, instance, inout_iid);
    if (!retval && (retval = _anjay_serv_object_validate(repr))) {
        (void) del_instance(repr, *inout_iid);
        if (!modified_since_persist) {
            /* validation failed and so in the end no instace is added */
            _anjay_serv_clear_modified(repr);
        }
    }

    if (!retval) {
        if (anjay_notify_instances_changed(anjay, SERVER.oid)) {
            server_log(WARNING, "Could not schedule socket reload");
        }
    }

    return retval;
}

static void server_purge(server_repr_t *repr) {
    if (repr->instances) {
        _anjay_serv_mark_modified(repr);
    }
    _anjay_serv_destroy_instances(&repr->instances);
    _anjay_serv_destroy_instances(&repr->saved_instances);
}

static void server_delete(anjay_t *anjay, void *repr) {
    (void) anjay;
    server_purge((server_repr_t *) repr);
    avs_free(repr);
}

void anjay_server_object_purge(anjay_t *anjay) {
    assert(anjay);

    const anjay_dm_object_def_t *const *server_obj =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    server_repr_t *repr = _anjay_serv_get(server_obj);

    server_purge(repr);

    if (anjay_notify_instances_changed(anjay, SERVER.oid)) {
        server_log(WARNING, "Could not schedule socket reload");
    }
}

bool anjay_server_object_is_modified(anjay_t *anjay) {
    assert(anjay);

    const anjay_dm_object_def_t *const *server_obj =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    return _anjay_serv_get(server_obj)->modified_since_persist;
}

static const anjay_dm_module_t SERVER_MODULE = {
    .deleter = server_delete
};

int anjay_server_object_install(anjay_t *anjay) {
    assert(anjay);

    server_repr_t *repr =
            (server_repr_t *) avs_calloc(1, sizeof(server_repr_t));
    if (!repr) {
        server_log(ERROR, "Out of memory");
        return -1;
    }

    repr->def = &SERVER;

    if (_anjay_dm_module_install(anjay, &SERVER_MODULE, repr)) {
        avs_free(repr);
        return -1;
    }

    if (anjay_register_object(anjay, &repr->def)) {
        // this will free repr
        int result = _anjay_dm_module_uninstall(anjay, &SERVER_MODULE);
        assert(!result);
        (void) result;
        return -1;
    }

    return 0;
}

#ifdef ANJAY_TEST
#    include "test/api.c"
#endif
