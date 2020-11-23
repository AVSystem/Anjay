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

#ifdef ANJAY_WITH_MODULE_SERVER

#    include <inttypes.h>
#    include <string.h>

#    include <avsystem/commons/avs_utils.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_servers.h>

#    include "anjay_mod_server.h"
#    include "anjay_server_transaction.h"
#    include "anjay_server_utils.h"

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
    if (*inout_iid == ANJAY_ID_INVALID) {
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
    if (*inout_iid == ANJAY_ID_INVALID) {
        if (assign_iid(repr, inout_iid)) {
            return -1;
        }
    } else if (find_instance(repr, *inout_iid)) {
        return -1;
    }
    AVS_LIST(server_instance_t) new_instance =
            AVS_LIST_NEW_ELEMENT(server_instance_t);
    if (!new_instance) {
        server_log(ERROR, _("out of memory"));
        return -1;
    }
    if (instance->binding) {
        if (!anjay_binding_mode_valid(instance->binding)
                || avs_simple_snprintf(new_instance->binding,
                                       sizeof(new_instance->binding), "%s",
                                       instance->binding)
                               < 0) {
            server_log(ERROR, _("Unsupported binding mode: ") "%s",
                       instance->binding);
            return -1;
        }
        new_instance->has_binding = true;
    }
    new_instance->iid = *inout_iid;
    new_instance->has_ssid = true;
    new_instance->ssid = instance->ssid;
    new_instance->has_lifetime = true;
    new_instance->lifetime = instance->lifetime;
    new_instance->has_default_min_period = (instance->default_min_period >= 0);
    if (new_instance->has_default_min_period) {
        new_instance->default_min_period = instance->default_min_period;
    }
    new_instance->has_default_max_period = (instance->default_max_period >= 0);
    if (new_instance->has_default_max_period) {
        new_instance->default_max_period = instance->default_max_period;
    }
#    ifndef ANJAY_WITHOUT_DEREGISTER
    new_instance->has_disable_timeout = (instance->disable_timeout >= 0);
    if (new_instance->has_disable_timeout) {
        new_instance->disable_timeout = instance->disable_timeout;
    }
#    endif // ANJAY_WITHOUT_DEREGISTER
    new_instance->has_notification_storing = true;
    new_instance->notification_storing = instance->notification_storing;

    if (insert_created_instance(repr, new_instance)) {
        AVS_LIST_CLEAR(&new_instance);
        return -1;
    }
    server_log(INFO, _("Added instance ") "%u" _(" (SSID: ") "%u" _(")"),
               *inout_iid, instance->ssid);
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

static int serv_list_instances(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    AVS_LIST(server_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        anjay_dm_emit(ctx, it->iid);
    }
    return 0;
}

static int serv_instance_create(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay;
    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    assert(iid != ANJAY_ID_INVALID);
    AVS_LIST(server_instance_t) created =
            AVS_LIST_NEW_ELEMENT(server_instance_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }
    created->iid = iid;
    _anjay_serv_reset_instance(created);

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
    anjay_ssid_t ssid = inst->ssid;
    _anjay_serv_reset_instance(inst);
    inst->has_ssid = has_ssid;
    inst->ssid = ssid;
    return 0;
}

static int serv_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    const server_instance_t *inst =
            find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);

    anjay_dm_emit_res(ctx, SERV_RES_SSID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SERV_RES_LIFETIME, ANJAY_DM_RES_RW,
                      inst->has_lifetime ? ANJAY_DM_RES_PRESENT
                                         : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SERV_RES_DEFAULT_MIN_PERIOD, ANJAY_DM_RES_RW,
                      inst->has_default_min_period ? ANJAY_DM_RES_PRESENT
                                                   : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SERV_RES_DEFAULT_MAX_PERIOD, ANJAY_DM_RES_RW,
                      inst->has_default_max_period ? ANJAY_DM_RES_PRESENT
                                                   : ANJAY_DM_RES_ABSENT);
#    ifndef ANJAY_WITHOUT_DEREGISTER
    anjay_dm_emit_res(ctx, SERV_RES_DISABLE, ANJAY_DM_RES_E,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SERV_RES_DISABLE_TIMEOUT, ANJAY_DM_RES_RW,
                      inst->has_disable_timeout ? ANJAY_DM_RES_PRESENT
                                                : ANJAY_DM_RES_ABSENT);
#    endif // ANJAY_WITHOUT_DEREGISTER
    anjay_dm_emit_res(ctx,
                      SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
                      ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SERV_RES_BINDING, ANJAY_DM_RES_RW,
                      inst->has_binding ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SERV_RES_REGISTRATION_UPDATE_TRIGGER, ANJAY_DM_RES_E,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int serv_read(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_riid_t riid,
                     anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);

    const server_instance_t *inst =
            find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);

    switch ((server_rid_t) rid) {
    case SERV_RES_SSID:
        return anjay_ret_i32(ctx, inst->ssid);
    case SERV_RES_LIFETIME:
        return anjay_ret_i32(ctx, inst->lifetime);
    case SERV_RES_DEFAULT_MIN_PERIOD:
        return anjay_ret_i32(ctx, inst->default_min_period);
    case SERV_RES_DEFAULT_MAX_PERIOD:
        return anjay_ret_i32(ctx, inst->default_max_period);
#    ifndef ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_DISABLE_TIMEOUT:
        return anjay_ret_i32(ctx, inst->disable_timeout);
#    endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        return anjay_ret_bool(ctx, inst->notification_storing);
    case SERV_RES_BINDING:
        return anjay_ret_string(ctx, inst->binding);
    default:
        AVS_UNREACHABLE(
                "Read called on unknown or non-readable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int serv_write(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj_ptr,
                      anjay_iid_t iid,
                      anjay_rid_t rid,
                      anjay_riid_t riid,
                      anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);

    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    server_instance_t *inst = find_instance(repr, iid);
    assert(inst);
    int retval;

    _anjay_serv_mark_modified(repr);

    switch ((server_rid_t) rid) {
    case SERV_RES_SSID:
        if (!(retval = _anjay_serv_fetch_ssid(ctx, &inst->ssid))) {
            inst->has_ssid = true;
        }
        return retval;
    case SERV_RES_LIFETIME:
        if (!(retval = anjay_get_i32(ctx, &inst->lifetime))) {
            inst->has_lifetime = true;
        }
        return retval;
    case SERV_RES_DEFAULT_MIN_PERIOD:
        if (!(retval = _anjay_serv_fetch_validated_i32(
                      ctx, 0, INT32_MAX, &inst->default_min_period))) {
            inst->has_default_min_period = true;
        }
        return retval;
    case SERV_RES_DEFAULT_MAX_PERIOD:
        if (!(retval = _anjay_serv_fetch_validated_i32(
                      ctx, 1, INT32_MAX, &inst->default_max_period))) {
            inst->has_default_max_period = true;
        }
        return retval;
#    ifndef ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_DISABLE_TIMEOUT:
        if (!(retval = _anjay_serv_fetch_validated_i32(
                      ctx, 0, INT32_MAX, &inst->disable_timeout))) {
            inst->has_disable_timeout = true;
        }
        return retval;
#    endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_BINDING:
        if (!(retval = _anjay_serv_fetch_binding(ctx, &inst->binding))) {
            inst->has_binding = true;
        }
        return retval;
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        if (!(retval = anjay_get_bool(ctx, &inst->notification_storing))) {
            inst->has_notification_storing = true;
        }
        return retval;
    default:
        AVS_UNREACHABLE(
                "Write called on unknown or non-read/writable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
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
#    ifndef ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_DISABLE: {
        avs_time_duration_t disable_timeout = avs_time_duration_from_scalar(
                inst->has_disable_timeout ? inst->disable_timeout : 86400,
                AVS_TIME_S);
        return anjay_disable_server_with_timeout(anjay, inst->ssid,
                                                 disable_timeout);
    }
#    endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_REGISTRATION_UPDATE_TRIGGER:
        return anjay_schedule_registration_update(anjay, inst->ssid)
                       ? ANJAY_ERR_BAD_REQUEST
                       : 0;
    default:
        AVS_UNREACHABLE(
                "Execute called on unknown or non-executable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

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
    .handlers = {
        .list_instances = serv_list_instances,
        .instance_create = serv_instance_create,
        .instance_remove = serv_instance_remove,
        .instance_reset = serv_instance_reset,
        .list_resources = serv_list_resources,
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
            server_log(WARNING, _("Could not schedule socket reload"));
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

static void server_delete(void *repr) {
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
        server_log(WARNING, _("Could not schedule socket reload"));
    }
}

AVS_LIST(const anjay_ssid_t) anjay_server_get_ssids(anjay_t *anjay) {
    assert(anjay);
    const anjay_dm_object_def_t *const *server_obj =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    server_repr_t *repr = _anjay_serv_get(server_obj);
    AVS_LIST(server_instance_t) source = NULL;
    if (_anjay_dm_transaction_object_included(anjay, server_obj)) {
        source = repr->saved_instances;
    } else {
        source = repr->instances;
    }
    // We rely on the fact that the "ssid" field is first in server_instance_t,
    // which means that both "source" and "&source->ssid" point to exactly the
    // same memory location. The "next" pointer location in AVS_LIST is
    // independent from the stored data type, so it's safe to do such "cast".
    AVS_STATIC_ASSERT(offsetof(server_instance_t, ssid) == 0,
                      instance_ssid_is_first_field);
    return &source->ssid;
}

bool anjay_server_object_is_modified(anjay_t *anjay) {
    assert(anjay);

    server_repr_t *repr =
            _anjay_serv_get(_anjay_dm_find_object_by_oid(anjay, SERVER.oid));
    return repr->in_transaction ? repr->saved_modified_since_persist
                                : repr->modified_since_persist;
}

size_t _anjay_server_object_get_instances_count(anjay_t *anjay) {
    const anjay_dm_object_def_t *const *server_obj =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    server_repr_t *repr = _anjay_serv_get(server_obj);

    size_t count = 0;
    server_instance_t *inst;
    AVS_LIST_FOREACH(inst, repr->instances) {
        count++;
    }
    return count;
}

static const anjay_dm_module_t SERVER_MODULE = {
    .deleter = server_delete
};

int anjay_server_object_install(anjay_t *anjay) {
    assert(anjay);

    server_repr_t *repr =
            (server_repr_t *) avs_calloc(1, sizeof(server_repr_t));
    if (!repr) {
        server_log(ERROR, _("out of memory"));
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

#    ifdef ANJAY_TEST
#        include "tests/modules/server/api.c"
#    endif

#endif // ANJAY_WITH_MODULE_SERVER
