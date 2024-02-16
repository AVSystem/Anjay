#include <inttypes.h>
#include <string.h>

#include "standalone_mod_server.h"
#include "standalone_server_transaction.h"
#include "standalone_server_utils.h"

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
#ifndef ANJAY_WITHOUT_DEREGISTER
    {
        .rid = SERV_RES_DISABLE,
        .kind = ANJAY_DM_RES_E
    },
    {
        .rid = SERV_RES_DISABLE_TIMEOUT,
        .kind = ANJAY_DM_RES_RW
    },
#endif // ANJAY_WITHOUT_DEREGISTER
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
#ifdef ANJAY_WITH_LWM2M11
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
#    ifdef ANJAY_WITH_SMS
    {
        .rid = SERV_RES_TRIGGER,
        .kind = ANJAY_DM_RES_RW
    },
#    endif // ANJAY_WITH_SMS
    {
        .rid = SERV_RES_PREFERRED_TRANSPORT,
        .kind = ANJAY_DM_RES_RW
    },
#    ifdef ANJAY_WITH_SEND
    {
        .rid = SERV_RES_MUTE_SEND,
        .kind = ANJAY_DM_RES_RW
    },
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11
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
    _standalone_serv_mark_modified(repr);
    AVS_LIST_INSERT(ptr, new_instance);
}

static int add_instance(server_repr_t *repr,
                        const standalone_server_instance_t *instance,
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
#ifndef ANJAY_WITHOUT_DEREGISTER
    new_instance->present_resources[SERV_RES_DISABLE] = true;
    if (instance->disable_timeout >= 0) {
        new_instance->present_resources[SERV_RES_DISABLE_TIMEOUT] = true;
        new_instance->disable_timeout = instance->disable_timeout;
    }
#endif // ANJAY_WITHOUT_DEREGISTER
    new_instance->present_resources
            [SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE] = true;
    new_instance->notification_storing = instance->notification_storing;
    new_instance->present_resources[SERV_RES_REGISTRATION_UPDATE_TRIGGER] =
            true;
#ifdef ANJAY_WITH_LWM2M11
#    ifdef ANJAY_WITH_BOOTSTRAP
    new_instance->present_resources[SERV_RES_BOOTSTRAP_REQUEST_TRIGGER] = true;
    new_instance
            ->present_resources[SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE] =
            true;
#    endif // ANJAY_WITH_BOOTSTRAP
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
#    ifdef ANJAY_WITH_SMS
    if (instance->trigger) {
        new_instance->present_resources[SERV_RES_TRIGGER] = true;
        new_instance->trigger = *instance->trigger;
    }
#    endif // ANJAY_WITH_SMS
    if (instance->communication_sequence_delay_timer) {
        new_instance->present_resources
                [SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER] = true;
        new_instance->server_communication_sequence_delay_timer =
                *instance->communication_sequence_delay_timer;
    }
#    ifdef ANJAY_WITH_SEND
    new_instance->present_resources[SERV_RES_MUTE_SEND] = true;
    new_instance->mute_send = instance->mute_send;
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11

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
            _standalone_serv_mark_modified(repr);
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

    server_repr_t *repr = _standalone_serv_get(obj_ptr);
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
    server_repr_t *repr = _standalone_serv_get(obj_ptr);
    assert(iid != ANJAY_ID_INVALID);
    AVS_LIST(server_instance_t) created =
            AVS_LIST_NEW_ELEMENT(server_instance_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }
    created->iid = iid;
    _standalone_serv_reset_instance(created);

    insert_created_instance(repr, created);
    return 0;
}

static int serv_instance_remove(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay;
    return del_instance(_standalone_serv_get(obj_ptr), iid);
}

static int serv_instance_reset(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    server_instance_t *inst = find_instance(_standalone_serv_get(obj_ptr), iid);
    assert(inst);

    anjay_ssid_t ssid = inst->ssid;
    _standalone_serv_reset_instance(inst);
    inst->present_resources[SERV_RES_SSID] = true;
    inst->ssid = ssid;
    return 0;
}

static int serv_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    const server_instance_t *inst =
            find_instance(_standalone_serv_get(obj_ptr), iid);
    assert(inst);

    for (size_t resource = 0; resource < AVS_ARRAY_SIZE(SERVER_RESOURCE_INFO);
         resource++) {
        anjay_rid_t rid = SERVER_RESOURCE_INFO[resource].rid;
        anjay_dm_emit_res(ctx, rid, SERVER_RESOURCE_INFO[resource].kind,
                          inst->present_resources[rid] ? ANJAY_DM_RES_PRESENT
                                                       : ANJAY_DM_RES_ABSENT);
    }

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
            find_instance(_standalone_serv_get(obj_ptr), iid);
    assert(inst);

    switch ((server_rid_t) rid) {
    case SERV_RES_SSID:
        return anjay_ret_i64(ctx, inst->ssid);
    case SERV_RES_LIFETIME:
        return anjay_ret_i64(ctx, inst->lifetime);
    case SERV_RES_DEFAULT_MIN_PERIOD:
        return anjay_ret_i64(ctx, inst->default_min_period);
    case SERV_RES_DEFAULT_MAX_PERIOD:
        return anjay_ret_i64(ctx, inst->default_max_period);
#ifndef ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_DISABLE_TIMEOUT:
        return anjay_ret_i64(ctx, inst->disable_timeout);
#endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        return anjay_ret_bool(ctx, inst->notification_storing);
    case SERV_RES_BINDING:
        return anjay_ret_string(ctx, inst->binding.data);
#ifdef ANJAY_WITH_LWM2M11
    case SERV_RES_TLS_DTLS_ALERT_CODE:
        return anjay_ret_u64(ctx, inst->last_alert);
    case SERV_RES_LAST_BOOTSTRAPPED:
        return anjay_ret_i64(ctx, inst->last_bootstrapped_timestamp);
#    ifdef ANJAY_WITH_BOOTSTRAP
    case SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE:
        return anjay_ret_bool(ctx, inst->bootstrap_on_registration_failure);
#    endif // ANJAY_WITH_BOOTSTRAP
    case SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT:
        return anjay_ret_u64(ctx, inst->server_communication_retry_count);
    case SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER:
        return anjay_ret_u64(ctx, inst->server_communication_retry_timer);
    case SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT:
        return anjay_ret_u64(ctx,
                             inst->server_communication_sequence_retry_count);
    case SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER:
        return anjay_ret_u64(ctx,
                             inst->server_communication_sequence_delay_timer);
#    ifdef ANJAY_WITH_SMS
    case SERV_RES_TRIGGER:
        return anjay_ret_bool(ctx, inst->trigger);
#    endif // ANJAY_WITH_SMS
    case SERV_RES_PREFERRED_TRANSPORT: {
        char tmp[2] = { inst->preferred_transport, '\0' };
        return anjay_ret_string(ctx, tmp);
    }
#    ifdef ANJAY_WITH_SEND
    case SERV_RES_MUTE_SEND:
        return anjay_ret_bool(ctx, inst->mute_send);
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11
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

    server_repr_t *repr = _standalone_serv_get(obj_ptr);
    server_instance_t *inst = find_instance(repr, iid);
    assert(inst);
    int retval;

    _standalone_serv_mark_modified(repr);

    switch ((server_rid_t) rid) {
    case SERV_RES_SSID:
        retval = _standalone_serv_fetch_ssid(ctx, &inst->ssid);
        break;
    case SERV_RES_LIFETIME:
        retval = anjay_get_i32(ctx, &inst->lifetime);
        break;
    case SERV_RES_DEFAULT_MIN_PERIOD:
        retval =
                _standalone_serv_fetch_validated_i32(ctx, 0, INT32_MAX,
                                                     &inst->default_min_period);
        break;
    case SERV_RES_DEFAULT_MAX_PERIOD:
        retval =
                _standalone_serv_fetch_validated_i32(ctx, 1, INT32_MAX,
                                                     &inst->default_max_period);
        break;
#ifndef ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_DISABLE_TIMEOUT:
        retval = _standalone_serv_fetch_validated_i32(ctx, 0, INT32_MAX,
                                                      &inst->disable_timeout);
        break;
#endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_BINDING:
        retval = _standalone_serv_fetch_binding(ctx, &inst->binding);
        break;
    case SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        retval = anjay_get_bool(ctx, &inst->notification_storing);
        break;
#ifdef ANJAY_WITH_LWM2M11
    case SERV_RES_TLS_DTLS_ALERT_CODE: {
        uint32_t last_alert;
        if (!(retval = anjay_get_u32(ctx, &last_alert))) {
            inst->last_alert = (uint8_t) last_alert;
        }
        break;
    }
    case SERV_RES_LAST_BOOTSTRAPPED:
        retval = anjay_get_i64(ctx, &inst->last_bootstrapped_timestamp);
        break;
#    ifdef ANJAY_WITH_BOOTSTRAP
    case SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE:
        retval = anjay_get_bool(ctx, &inst->bootstrap_on_registration_failure);
        break;
#    endif // ANJAY_WITH_BOOTSTRAP
    case SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT:
        if (!(retval = anjay_get_u32(ctx,
                                     &inst->server_communication_retry_count))
                && inst->server_communication_retry_count == 0) {
            server_log(ERROR, "Server Communication Retry Count cannot be 0");
            retval = ANJAY_ERR_BAD_REQUEST;
        }
        break;
    case SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER:
        retval = anjay_get_u32(ctx, &inst->server_communication_retry_timer);
        break;
    case SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT:
        if (!(retval = anjay_get_u32(
                      ctx, &inst->server_communication_sequence_retry_count))
                && inst->server_communication_sequence_retry_count == 0) {
            server_log(ERROR,
                       "Server Sequence Communication Retry Count cannot be 0");
            retval = ANJAY_ERR_BAD_REQUEST;
        }
        break;
    case SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER:
        retval =
                anjay_get_u32(ctx,
                              &inst->server_communication_sequence_delay_timer);
        break;
#    ifdef ANJAY_WITH_SMS
    case SERV_RES_TRIGGER:
        retval = anjay_get_bool(ctx, &inst->trigger);
        break;
#    endif // ANJAY_WITH_SMS
    case SERV_RES_PREFERRED_TRANSPORT: {
        char tmp[2];
        if (!(retval = anjay_get_string(ctx, tmp, sizeof(tmp)))) {
            inst->preferred_transport = tmp[0];
        } else if (retval == ANJAY_BUFFER_TOO_SHORT) {
            retval = ANJAY_ERR_BAD_REQUEST;
        }
        break;
    }
#    ifdef ANJAY_WITH_SEND
    case SERV_RES_MUTE_SEND:
        retval = anjay_get_bool(ctx, &inst->mute_send);
        break;
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11
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

static int serv_execute(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj_ptr,
                        anjay_iid_t iid,
                        anjay_rid_t rid,
                        anjay_execute_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) ctx;
    server_instance_t *inst = find_instance(_standalone_serv_get(obj_ptr), iid);
    assert(inst);

    switch ((server_rid_t) rid) {
#ifndef ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_DISABLE: {
        avs_time_duration_t disable_timeout = avs_time_duration_from_scalar(
                inst->present_resources[SERV_RES_DISABLE_TIMEOUT]
                        ? inst->disable_timeout
                        : 86400,
                AVS_TIME_S);
        return anjay_disable_server_with_timeout(anjay, inst->ssid,
                                                 disable_timeout);
    }
#endif // ANJAY_WITHOUT_DEREGISTER
    case SERV_RES_REGISTRATION_UPDATE_TRIGGER:
        return anjay_schedule_registration_update(anjay, inst->ssid)
                       ? ANJAY_ERR_BAD_REQUEST
                       : 0;
#if defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_BOOTSTRAP)
    case SERV_RES_BOOTSTRAP_REQUEST_TRIGGER:
        return anjay_schedule_bootstrap_request(anjay)
                       ? ANJAY_ERR_METHOD_NOT_ALLOWED
                       : 0;
#endif // defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_BOOTSTRAP)
    default:
        AVS_UNREACHABLE(
                "Execute called on unknown or non-executable Server resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int serv_transaction_begin(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _standalone_serv_transaction_begin_impl(
            _standalone_serv_get(obj_ptr));
}

static int
serv_transaction_commit(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _standalone_serv_transaction_commit_impl(
            _standalone_serv_get(obj_ptr));
}

static int
serv_transaction_validate(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _standalone_serv_transaction_validate_impl(
            _standalone_serv_get(obj_ptr));
}

static int
serv_transaction_rollback(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _standalone_serv_transaction_rollback_impl(
            _standalone_serv_get(obj_ptr));
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

server_repr_t *
_standalone_serv_get(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr && *obj_ptr == &SERVER);
    return AVS_CONTAINER_OF(obj_ptr, server_repr_t, def);
}

int standalone_server_object_add_instance(
        const anjay_dm_object_def_t *const *obj_ptr,
        const standalone_server_instance_t *instance,
        anjay_iid_t *inout_iid) {
    int retval = -1;
    server_repr_t *repr = _standalone_serv_get(obj_ptr);
    if (!repr) {
        server_log(ERROR, _("Server object is not registered"));
        retval = -1;
    } else {
        const bool modified_since_persist = repr->modified_since_persist;
        if (!(retval = add_instance(repr, instance, inout_iid))
                && (retval = _standalone_serv_object_validate(repr))) {
            (void) del_instance(repr, *inout_iid);
            if (!modified_since_persist) {
                /* validation failed and so in the end no instace is added */
                _standalone_serv_clear_modified(repr);
            }
        }

        if (!retval) {
            if (anjay_notify_instances_changed(repr->anjay, SERVER.oid)) {
                server_log(WARNING, _("Could not schedule socket reload"));
            }
        }
    }
    return retval;
}

static void server_purge(server_repr_t *repr) {
    if (repr->instances) {
        _standalone_serv_mark_modified(repr);
    }
    _standalone_serv_destroy_instances(&repr->instances);
    _standalone_serv_destroy_instances(&repr->saved_instances);
}

void standalone_server_object_cleanup(
        const anjay_dm_object_def_t *const *obj_ptr) {
    server_repr_t *repr = _standalone_serv_get(obj_ptr);
    server_purge(repr);
    avs_free(repr);
}

void standalone_server_object_purge(
        const anjay_dm_object_def_t *const *obj_ptr) {
    server_repr_t *repr = _standalone_serv_get(obj_ptr);

    if (!repr) {
        server_log(ERROR, _("Server object is not registered"));
    } else {
        server_purge(repr);
        if (anjay_notify_instances_changed(repr->anjay, SERVER.oid)) {
            server_log(WARNING, _("Could not schedule socket reload"));
        }
    }
}

AVS_LIST(const anjay_ssid_t)
standalone_server_get_ssids(const anjay_dm_object_def_t *const *obj_ptr) {
    AVS_LIST(server_instance_t) source = NULL;
    server_repr_t *repr = _standalone_serv_get(obj_ptr);
    if (repr->in_transaction) {
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

bool standalone_server_object_is_modified(
        const anjay_dm_object_def_t *const *obj_ptr) {
    bool result = false;
    server_repr_t *repr = _standalone_serv_get(obj_ptr);
    if (!repr) {
        server_log(ERROR, _("Server object is not registered"));
    } else {
        if (repr->in_transaction) {
            result = repr->saved_modified_since_persist;
        } else {
            result = repr->modified_since_persist;
        }
    }
    return result;
}

const anjay_dm_object_def_t **standalone_server_object_install(anjay_t *anjay) {
    assert(anjay);
    server_repr_t *repr =
            (server_repr_t *) avs_calloc(1, sizeof(server_repr_t));
    if (!repr) {
        server_log(ERROR, _("out of memory"));
        return NULL;
    }
    repr->def = &SERVER;
    repr->anjay = anjay;
    if (anjay_register_object(anjay, &repr->def)) {
        avs_free(repr);
        return NULL;
    }
    return &repr->def;
}

int standalone_server_object_set_lifetime(
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        int32_t lifetime) {
    if (lifetime <= 0) {
        server_log(ERROR, _("lifetime MUST BE strictly positive"));
        return -1;
    }
    int result = -1;
    server_repr_t *repr = _standalone_serv_get(obj_ptr);
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
            if (anjay_notify_changed(repr->anjay, ANJAY_DM_OID_SERVER, it->iid,
                                     SERV_RES_LIFETIME)) {
                server_log(WARNING, _("could not notify lifetime change"));
            }
            repr->modified_since_persist = true;
            it->lifetime = lifetime;
            result = 0;
        }
    }
    return result;
}
