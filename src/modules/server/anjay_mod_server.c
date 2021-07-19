/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#    include <anjay/server.h>

#    include <anjay_modules/anjay_bootstrap.h>
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
                || avs_simple_snprintf(new_instance->binding.data,
                                       sizeof(new_instance->binding.data), "%s",
                                       instance->binding)
                               < 0) {
            server_log(ERROR, _("Unsupported binding mode: ") "%s",
                       instance->binding);
            AVS_LIST_CLEAR(&new_instance);
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

static int serv_list_instances(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_ptr,
                               anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    server_repr_t *repr = _anjay_serv_get(obj_ptr);
    AVS_LIST(server_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        _anjay_dm_emit_unlocked(ctx, it->iid);
    }
    return 0;
}

static int serv_instance_create(anjay_unlocked_t *anjay,
                                const anjay_dm_installed_object_t obj_ptr,
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

static int serv_instance_remove(anjay_unlocked_t *anjay,
                                const anjay_dm_installed_object_t obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay;
    return del_instance(_anjay_serv_get(obj_ptr), iid);
}

static int serv_instance_reset(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_ptr,
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

static int serv_list_resources(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_ptr,
                               anjay_iid_t iid,
                               anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    const server_instance_t *inst =
            find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);

    _anjay_dm_emit_res_unlocked(ctx, SERV_RES_SSID, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, SERV_RES_LIFETIME, ANJAY_DM_RES_RW,
                                inst->has_lifetime ? ANJAY_DM_RES_PRESENT
                                                   : ANJAY_DM_RES_ABSENT);
    _anjay_dm_emit_res_unlocked(ctx, SERV_RES_DEFAULT_MIN_PERIOD,
                                ANJAY_DM_RES_RW,
                                inst->has_default_min_period
                                        ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
    _anjay_dm_emit_res_unlocked(ctx, SERV_RES_DEFAULT_MAX_PERIOD,
                                ANJAY_DM_RES_RW,
                                inst->has_default_max_period
                                        ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
#    ifndef ANJAY_WITHOUT_DEREGISTER
    _anjay_dm_emit_res_unlocked(ctx, SERV_RES_DISABLE, ANJAY_DM_RES_E,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, SERV_RES_DISABLE_TIMEOUT, ANJAY_DM_RES_RW,
                                inst->has_disable_timeout
                                        ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
#    endif // ANJAY_WITHOUT_DEREGISTER
    _anjay_dm_emit_res_unlocked(
            ctx, SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, SERV_RES_BINDING, ANJAY_DM_RES_RW,
                                inst->has_binding ? ANJAY_DM_RES_PRESENT
                                                  : ANJAY_DM_RES_ABSENT);
    _anjay_dm_emit_res_unlocked(ctx, SERV_RES_REGISTRATION_UPDATE_TRIGGER,
                                ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int serv_read(anjay_unlocked_t *anjay,
                     const anjay_dm_installed_object_t obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_riid_t riid,
                     anjay_unlocked_output_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);

    const server_instance_t *inst =
            find_instance(_anjay_serv_get(obj_ptr), iid);
    assert(inst);

    switch ((server_rid_t) rid) {
    case SERV_RES_SSID:
        return _anjay_ret_i64_unlocked(ctx, inst->ssid);
    case SERV_RES_LIFETIME:
        return _anjay_ret_i64_unlocked(ctx, inst->lifetime);
    case SERV_RES_DEFAULT_MIN_PERIOD:
        return _anjay_ret_i64_unlocked(ctx, inst->default_min_period);
    case SERV_RES_DEFAULT_MAX_PERIOD:
        return _anjay_ret_i64_unlocked(ctx, inst->default_max_period);
#    ifndef ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_DISABLE_TIMEOUT:
        return _anjay_ret_i64_unlocked(ctx, inst->disable_timeout);
#    endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        return _anjay_ret_bool_unlocked(ctx, inst->notification_storing);
    case SERV_RES_BINDING:
        return _anjay_ret_string_unlocked(ctx, inst->binding.data);
    default:
        AVS_UNREACHABLE(
                "Read called on unknown or non-readable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int serv_write(anjay_unlocked_t *anjay,
                      const anjay_dm_installed_object_t obj_ptr,
                      anjay_iid_t iid,
                      anjay_rid_t rid,
                      anjay_riid_t riid,
                      anjay_unlocked_input_ctx_t *ctx) {
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
        if (!(retval = _anjay_get_i32_unlocked(ctx, &inst->lifetime))) {
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
        if (!(retval = _anjay_get_bool_unlocked(ctx,
                                                &inst->notification_storing))) {
            inst->has_notification_storing = true;
        }
        return retval;
    default:
        AVS_UNREACHABLE(
                "Write called on unknown or non-read/writable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int serv_execute(anjay_unlocked_t *anjay,
                        const anjay_dm_installed_object_t obj_ptr,
                        anjay_iid_t iid,
                        anjay_rid_t rid,
                        anjay_unlocked_execute_ctx_t *ctx) {
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
        return _anjay_disable_server_with_timeout_unlocked(anjay, inst->ssid,
                                                           disable_timeout);
    }
#    endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_REGISTRATION_UPDATE_TRIGGER:
        return _anjay_schedule_registration_update_unlocked(anjay, inst->ssid)
                       ? ANJAY_ERR_BAD_REQUEST
                       : 0;
    default:
        AVS_UNREACHABLE(
                "Execute called on unknown or non-executable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    return ANJAY_ERR_NOT_IMPLEMENTED;
}

static int serv_transaction_begin(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    return _anjay_serv_transaction_begin_impl(_anjay_serv_get(obj_ptr));
}

static int serv_transaction_commit(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    return _anjay_serv_transaction_commit_impl(_anjay_serv_get(obj_ptr));
}

static int
serv_transaction_validate(anjay_unlocked_t *anjay,
                          const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    return _anjay_serv_transaction_validate_impl(_anjay_serv_get(obj_ptr));
}

static int
serv_transaction_rollback(anjay_unlocked_t *anjay,
                          const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    return _anjay_serv_transaction_rollback_impl(_anjay_serv_get(obj_ptr));
}

static const anjay_unlocked_dm_object_def_t SERVER = {
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

server_repr_t *_anjay_serv_get(const anjay_dm_installed_object_t obj_ptr) {
    const anjay_unlocked_dm_object_def_t *const *unlocked_def =
            _anjay_dm_installed_object_get_unlocked(&obj_ptr);
    assert(*unlocked_def == &SERVER);
    return AVS_CONTAINER_OF(unlocked_def, server_repr_t, def);
}

int anjay_server_object_add_instance(anjay_t *anjay_locked,
                                     const anjay_server_instance_t *instance,
                                     anjay_iid_t *inout_iid) {
    assert(anjay_locked);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj_ptr =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    server_repr_t *repr = obj_ptr ? _anjay_serv_get(*obj_ptr) : NULL;
    if (!repr) {
        server_log(ERROR, _("Server object is not registered"));
        retval = -1;
    } else {
        const bool modified_since_persist = repr->modified_since_persist;
        if (!(retval = add_instance(repr, instance, inout_iid))
                && (retval = _anjay_serv_object_validate(repr))) {
            (void) del_instance(repr, *inout_iid);
            if (!modified_since_persist) {
                /* validation failed and so in the end no instace is added */
                _anjay_serv_clear_modified(repr);
            }
        }

        if (!retval) {
            if (_anjay_notify_instances_changed_unlocked(anjay, SERVER.oid)) {
                server_log(WARNING, _("Could not schedule socket reload"));
            }
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
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
    // NOTE: repr itself will be freed when cleaning the objects list
}

void anjay_server_object_purge(anjay_t *anjay_locked) {
    assert(anjay_locked);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *server_obj =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    server_repr_t *repr = server_obj ? _anjay_serv_get(*server_obj) : NULL;

    if (!repr) {
        server_log(ERROR, _("Server object is not registered"));
    } else {
        server_purge(repr);
        if (_anjay_notify_instances_changed_unlocked(anjay, SERVER.oid)) {
            server_log(WARNING, _("Could not schedule socket reload"));
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

AVS_LIST(const anjay_ssid_t) anjay_server_get_ssids(anjay_t *anjay_locked) {
    assert(anjay_locked);
    AVS_LIST(server_instance_t) source = NULL;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *server_obj =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    server_repr_t *repr = _anjay_serv_get(*server_obj);
    if (_anjay_dm_transaction_object_included(anjay, server_obj)) {
        source = repr->saved_instances;
    } else {
        source = repr->instances;
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    // We rely on the fact that the "ssid" field is first in server_instance_t,
    // which means that both "source" and "&source->ssid" point to exactly the
    // same memory location. The "next" pointer location in AVS_LIST is
    // independent from the stored data type, so it's safe to do such "cast".
    AVS_STATIC_ASSERT(offsetof(server_instance_t, ssid) == 0,
                      instance_ssid_is_first_field);
    return &source->ssid;
}

bool anjay_server_object_is_modified(anjay_t *anjay_locked) {
    assert(anjay_locked);
    bool result = false;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *server_obj =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    if (!server_obj) {
        server_log(ERROR, _("Server object is not registered"));
    } else {
        server_repr_t *repr = _anjay_serv_get(*server_obj);
        if (repr->in_transaction) {
            result = repr->saved_modified_since_persist;
        } else {
            result = repr->modified_since_persist;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static const anjay_dm_module_t SERVER_MODULE = {
    .deleter = server_delete
};

int anjay_server_object_install(anjay_t *anjay_locked) {
    assert(anjay_locked);
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    AVS_LIST(server_repr_t) repr = AVS_LIST_NEW_ELEMENT(server_repr_t);
    if (!repr) {
        server_log(ERROR, _("out of memory"));
    } else {
        repr->def = &SERVER;
        _anjay_dm_installed_object_init_unlocked(&repr->def_ptr, &repr->def);
        if (!_anjay_dm_module_install(anjay, &SERVER_MODULE, repr)) {
            AVS_STATIC_ASSERT(offsetof(server_repr_t, def_ptr) == 0,
                              def_ptr_is_first_field);
            AVS_LIST(anjay_dm_installed_object_t) entry = &repr->def_ptr;
            if (_anjay_register_object_unlocked(anjay, &entry)) {
                result = _anjay_dm_module_uninstall(anjay, &SERVER_MODULE);
                assert(!result);
                result = -1;
            } else {
                result = 0;
            }
        }
        if (result) {
            AVS_LIST_CLEAR(&repr);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

#    ifdef ANJAY_TEST
#        include "tests/modules/server/api.c"
#    endif

#endif // ANJAY_WITH_MODULE_SERVER
