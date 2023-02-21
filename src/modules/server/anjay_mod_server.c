/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_SERVER

#    include <inttypes.h>
#    include <string.h>

#    include <anjay/server.h>

#    include <anjay_modules/anjay_bootstrap.h>
#    include <anjay_modules/anjay_servers.h>

#    include "anjay_mod_server.h"
#    include "anjay_server_transaction.h"
#    include "anjay_server_utils.h"

VISIBILITY_SOURCE_BEGIN

static const struct {
    server_rid_t rid;
    anjay_dm_resource_kind_t kind;
} SERVER_RESOURCE_INFO[] = {
    {
        .rid = SERV_RES_SSID,
        .kind = ANJAY_DM_RES_R
    },
    {
        .rid = SERV_RES_LIFETIME,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_DEFAULT_MIN_PERIOD,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_DEFAULT_MAX_PERIOD,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_DISABLE,
        .kind = ANJAY_DM_RES_E
    },
    {
        .rid = SERV_RES_DISABLE_TIMEOUT,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_BINDING,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_REGISTRATION_UPDATE_TRIGGER,
        .kind = ANJAY_DM_RES_E
    },
#    ifdef ANJAY_WITH_LWM2M11
    {
        .rid = SERV_RES_BOOTSTRAP_REQUEST_TRIGGER,
        .kind = ANJAY_DM_RES_E
    },
    {
        .rid = SERV_RES_TLS_DTLS_ALERT_CODE,
        .kind = ANJAY_DM_RES_R
    },
    {
        .rid = SERV_RES_LAST_BOOTSTRAPPED,
        .kind = ANJAY_DM_RES_R
    },
    {
        .rid = SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE,
        .kind = ANJAY_DM_RES_R
    },
    {
        .rid = SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT,
        .kind = ANJAY_DM_RES_RW
    },
    {
        .rid = SERV_RES_PREFERRED_TRANSPORT,
        .kind = ANJAY_DM_RES_RW
    },
#        ifdef ANJAY_WITH_SEND
    {
        .rid = SERV_RES_MUTE_SEND,
        .kind = ANJAY_DM_RES_RW
    },
#        endif // ANJAY_WITH_SEND
#    endif     // ANJAY_WITH_LWM2M11
};

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

static void insert_created_instance(server_repr_t *repr,
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
        new_instance->present_resources[SERV_RES_BINDING] = true;
    }
    new_instance->iid = *inout_iid;
    new_instance->present_resources[SERV_RES_SSID] = true;
    new_instance->ssid = instance->ssid;
    new_instance->present_resources[SERV_RES_LIFETIME] = true;
    new_instance->lifetime = instance->lifetime;
    if (instance->default_min_period >= 0) {
        new_instance->present_resources[SERV_RES_DEFAULT_MIN_PERIOD] = true;
        new_instance->default_min_period = instance->default_min_period;
    }

    if (instance->default_max_period >= 0) {
        new_instance->present_resources[SERV_RES_DEFAULT_MAX_PERIOD] = true;
        new_instance->default_max_period = instance->default_max_period;
    }
#    ifndef ANJAY_WITHOUT_DEREGISTER
    new_instance->present_resources[SERV_RES_DISABLE] = true;
    if (instance->disable_timeout >= 0) {
        new_instance->present_resources[SERV_RES_DISABLE_TIMEOUT] = true;
        new_instance->disable_timeout = instance->disable_timeout;
    }
#    endif // ANJAY_WITHOUT_DEREGISTER
    new_instance->present_resources
            [SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE] = true;
    new_instance->notification_storing = instance->notification_storing;
    new_instance->present_resources[SERV_RES_REGISTRATION_UPDATE_TRIGGER] =
            true;
#    ifdef ANJAY_WITH_LWM2M11
#        ifdef ANJAY_WITH_BOOTSTRAP
    new_instance->present_resources[SERV_RES_BOOTSTRAP_REQUEST_TRIGGER] = true;
    new_instance
            ->present_resources[SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE] =
            true;
#        endif // ANJAY_WITH_BOOTSTRAP
    new_instance->bootstrap_on_registration_failure =
            instance->bootstrap_on_registration_failure
                    ? *instance->bootstrap_on_registration_failure
                    : true;
    if (instance->communication_retry_count) {
        new_instance
                ->present_resources[SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT] =
                true;
        new_instance->server_communication_retry_count =
                *instance->communication_retry_count;
    }
    if (instance->communication_retry_timer) {
        new_instance
                ->present_resources[SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER] =
                true;
        new_instance->server_communication_retry_timer =
                *instance->communication_retry_timer;
    }
    if (instance->preferred_transport) {
        new_instance->preferred_transport = instance->preferred_transport;
        new_instance->present_resources[SERV_RES_PREFERRED_TRANSPORT] = true;
    }
    if (instance->communication_sequence_retry_count) {
        new_instance->present_resources
                [SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT] = true;
        new_instance->server_communication_sequence_retry_count =
                *instance->communication_sequence_retry_count;
    }
    if (instance->communication_sequence_delay_timer) {
        new_instance->present_resources
                [SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER] = true;
        new_instance->server_communication_sequence_delay_timer =
                *instance->communication_sequence_delay_timer;
    }
#        ifdef ANJAY_WITH_SEND
    new_instance->present_resources[SERV_RES_MUTE_SEND] = true;
    new_instance->mute_send = instance->mute_send;
#        endif // ANJAY_WITH_SEND
#    endif     // ANJAY_WITH_LWM2M11

    insert_created_instance(repr, new_instance);
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

    insert_created_instance(repr, created);
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

    anjay_ssid_t ssid = inst->ssid;
    _anjay_serv_reset_instance(inst);
    inst->present_resources[SERV_RES_SSID] = true;
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

    for (size_t resource = 0; resource < AVS_ARRAY_SIZE(SERVER_RESOURCE_INFO);
         resource++) {
        anjay_rid_t rid = SERVER_RESOURCE_INFO[resource].rid;
        _anjay_dm_emit_res_unlocked(ctx, rid,
                                    SERVER_RESOURCE_INFO[resource].kind,
                                    inst->present_resources[rid]
                                            ? ANJAY_DM_RES_PRESENT
                                            : ANJAY_DM_RES_ABSENT);
    }

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
#    ifdef ANJAY_WITH_LWM2M11
    case SERV_RES_TLS_DTLS_ALERT_CODE:
        return _anjay_ret_u64_unlocked(ctx, inst->last_alert);
    case SERV_RES_LAST_BOOTSTRAPPED:
        return _anjay_ret_i64_unlocked(ctx, inst->last_bootstrapped_timestamp);
#        ifdef ANJAY_WITH_BOOTSTRAP
    case SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE:
        return _anjay_ret_bool_unlocked(
                ctx, inst->bootstrap_on_registration_failure);
#        endif // ANJAY_WITH_BOOTSTRAP
    case SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT:
        return _anjay_ret_u64_unlocked(ctx,
                                       inst->server_communication_retry_count);
    case SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER:
        return _anjay_ret_u64_unlocked(ctx,
                                       inst->server_communication_retry_timer);
    case SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT:
        return _anjay_ret_u64_unlocked(
                ctx, inst->server_communication_sequence_retry_count);
    case SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER:
        return _anjay_ret_u64_unlocked(
                ctx, inst->server_communication_sequence_delay_timer);
    case SERV_RES_PREFERRED_TRANSPORT: {
        char tmp[2] = { inst->preferred_transport, '\0' };
        return _anjay_ret_string_unlocked(ctx, tmp);
    }
#        ifdef ANJAY_WITH_SEND
    case SERV_RES_MUTE_SEND:
        return _anjay_ret_bool_unlocked(ctx, inst->mute_send);
#        endif // ANJAY_WITH_SEND
#    endif     // ANJAY_WITH_LWM2M11
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
        retval = _anjay_serv_fetch_ssid(ctx, &inst->ssid);
        break;
    case SERV_RES_LIFETIME:
        retval = _anjay_get_i32_unlocked(ctx, &inst->lifetime);
        break;
    case SERV_RES_DEFAULT_MIN_PERIOD:
        retval = _anjay_serv_fetch_validated_i32(ctx, 0, INT32_MAX,
                                                 &inst->default_min_period);
        break;
    case SERV_RES_DEFAULT_MAX_PERIOD:
        retval = _anjay_serv_fetch_validated_i32(ctx, 1, INT32_MAX,
                                                 &inst->default_max_period);
        break;
#    ifndef ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_DISABLE_TIMEOUT:
        retval = _anjay_serv_fetch_validated_i32(ctx, 0, INT32_MAX,
                                                 &inst->disable_timeout);
        break;
#    endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_BINDING:
        retval = _anjay_serv_fetch_binding(ctx, &inst->binding);
        break;
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        retval = _anjay_get_bool_unlocked(ctx, &inst->notification_storing);
        break;
#    ifdef ANJAY_WITH_LWM2M11
    case SERV_RES_TLS_DTLS_ALERT_CODE: {
        uint32_t last_alert;
        if (!(retval = _anjay_get_u32_unlocked(ctx, &last_alert))) {
            inst->last_alert = (uint8_t) last_alert;
        }
        break;
    }
    case SERV_RES_LAST_BOOTSTRAPPED:
        retval = _anjay_get_i64_unlocked(ctx,
                                         &inst->last_bootstrapped_timestamp);
        break;
#        ifdef ANJAY_WITH_BOOTSTRAP
    case SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE:
        retval = _anjay_get_bool_unlocked(
                ctx, &inst->bootstrap_on_registration_failure);
        break;
#        endif // ANJAY_WITH_BOOTSTRAP
    case SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT:
        if (!(retval = _anjay_get_u32_unlocked(
                      ctx, &inst->server_communication_retry_count))
                && inst->server_communication_retry_count == 0) {
            server_log(ERROR, "Server Communication Retry Count cannot be 0");
            retval = ANJAY_ERR_BAD_REQUEST;
        }
        break;
    case SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER:
        retval = _anjay_get_u32_unlocked(
                ctx, &inst->server_communication_retry_timer);
        break;
    case SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT:
        if (!(retval = _anjay_get_u32_unlocked(
                      ctx, &inst->server_communication_sequence_retry_count))
                && inst->server_communication_sequence_retry_count == 0) {
            server_log(ERROR,
                       "Server Sequence Communication Retry Count cannot be 0");
            retval = ANJAY_ERR_BAD_REQUEST;
        }
        break;
    case SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER:
        retval = _anjay_get_u32_unlocked(
                ctx, &inst->server_communication_sequence_delay_timer);
        break;
    case SERV_RES_PREFERRED_TRANSPORT: {
        char tmp[2];
        if (!(retval = _anjay_get_string_unlocked(ctx, tmp, sizeof(tmp)))) {
            inst->preferred_transport = tmp[0];
        } else if (retval == ANJAY_BUFFER_TOO_SHORT) {
            retval = ANJAY_ERR_BAD_REQUEST;
        }
        break;
    }
#        ifdef ANJAY_WITH_SEND
    case SERV_RES_MUTE_SEND:
        retval = _anjay_get_bool_unlocked(ctx, &inst->mute_send);
        break;
#        endif // ANJAY_WITH_SEND
#    endif     // ANJAY_WITH_LWM2M11
    default:
        AVS_UNREACHABLE(
                "Write called on unknown or non-read/writable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    if (!retval) {
        inst->present_resources[rid] = true;
    }

    return retval;
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
                inst->present_resources[SERV_RES_DISABLE_TIMEOUT]
                        ? inst->disable_timeout
                        : 86400,
                AVS_TIME_S);
        return _anjay_schedule_disable_server_with_explicit_timeout_unlocked(
                anjay, inst->ssid, disable_timeout);
    }
#    endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_REGISTRATION_UPDATE_TRIGGER:
        return _anjay_schedule_registration_update_unlocked(anjay, inst->ssid)
                       ? ANJAY_ERR_BAD_REQUEST
                       : 0;
#    if defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_BOOTSTRAP)
    case SERV_RES_BOOTSTRAP_REQUEST_TRIGGER:
        return _anjay_schedule_bootstrap_request_unlocked(anjay)
                       ? ANJAY_ERR_METHOD_NOT_ALLOWED
                       : 0;
#    endif // defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_BOOTSTRAP)
    default:
        AVS_UNREACHABLE(
                "Execute called on unknown or non-executable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
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
        if (!_anjay_dm_module_install(anjay, server_delete, repr)) {
            AVS_STATIC_ASSERT(offsetof(server_repr_t, def_ptr) == 0,
                              def_ptr_is_first_field);
            AVS_LIST(anjay_dm_installed_object_t) entry = &repr->def_ptr;
            if (_anjay_register_object_unlocked(anjay, &entry)) {
                result = _anjay_dm_module_uninstall(anjay, server_delete);
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

int anjay_server_object_set_lifetime(anjay_t *anjay_locked,
                                     anjay_iid_t iid,
                                     int32_t lifetime) {
    if (lifetime <= 0) {
        server_log(ERROR, _("lifetime MUST BE strictly positive"));
        return -1;
    }
    assert(anjay_locked);
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *server_obj =
            _anjay_dm_find_object_by_oid(anjay, SERVER.oid);
    server_repr_t *repr = _anjay_serv_get(*server_obj);
    if (repr->saved_instances) {
        server_log(ERROR, _("cannot set Lifetime while some transaction is "
                            "started on the Server Object"));
    } else {
        AVS_LIST(server_instance_t) it;
        AVS_LIST_FOREACH(it, repr->instances) {
            if (it->iid >= iid) {
                break;
            }
        }

        if (!it || it->iid != iid) {
            server_log(ERROR, _("instance ") "%" PRIu16 _(" not found"), iid);
        } else if (it->lifetime != lifetime) {
            if (_anjay_notify_changed_unlocked(anjay, ANJAY_DM_OID_SERVER,
                                               it->iid,
                                               ANJAY_DM_RID_SERVER_LIFETIME)) {
                server_log(WARNING, _("could not notify lifetime change"));
            }
            repr->modified_since_persist = true;
            it->lifetime = lifetime;
            result = 0;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

#    ifdef ANJAY_TEST
#        include "tests/modules/server/api.c"
#    endif

#endif // ANJAY_WITH_MODULE_SERVER
