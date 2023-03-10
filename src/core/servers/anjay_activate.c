/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_sched.h>

#include <inttypes.h>

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_servers_inactive.h"
#include "../anjay_servers_reload.h"
#include "../anjay_servers_utils.h"
#include "../dm/anjay_query.h"

#include "anjay_activate.h"
#include "anjay_register.h"
#include "anjay_server_connections.h"
#include "anjay_servers_internal.h"

VISIBILITY_SOURCE_BEGIN

/**
 * Deactivates the active server entry @p server .
 *
 * If <c>server->reactivate_time</c> is not AVS_TIME_DURATION_INVALID, schedules
 * a reactivate job for that time. The job is a retryable one, so the caller
 * does not need to worry about reactivating the server manually.
 */
static int deactivate_server(anjay_server_info_t *server) {
    assert(server);
#ifndef ANJAY_WITHOUT_DEREGISTER
    if (server->ssid != ANJAY_SSID_BOOTSTRAP
            && !_anjay_bootstrap_in_progress(server->anjay)
            && _anjay_server_active(server)
            && !_anjay_server_registration_expired(server)) {
        // Return value intentionally ignored.
        // There isn't much we can do in case it fails and De-Register is
        // optional anyway. _anjay_serve_deregister logs the error cause.
        _anjay_server_deregister(server);
    }
#endif // ANJAY_WITHOUT_DEREGISTER
    _anjay_server_clean_active_data(server);
    server->registration_info.expire_time = AVS_TIME_REAL_INVALID;
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        _anjay_connection_internal_invalidate_session(
                _anjay_connection_get(&server->connections, conn_type));
    }
    if (avs_time_real_valid(server->reactivate_time)
            && _anjay_server_sched_activate(server)) {
        // not much we can do other than removing the server altogether
        anjay_log(ERROR, _("could not reschedule server reactivation"));
        AVS_LIST(anjay_server_info_t) *server_ptr =
                _anjay_servers_find_ptr(&server->anjay->servers, server->ssid);
        assert(server_ptr);
        assert(*server_ptr == server);
        AVS_LIST_DELETE(server_ptr);
        return -1;
    }
    return 0;
}

#ifdef ANJAY_WITH_LWM2M11
static void try_read_server_resource_u32(anjay_server_info_t *server,
                                         anjay_rid_t rid,
                                         int64_t min_value,
                                         uint32_t *out_result) {
    anjay_iid_t server_iid = ANJAY_ID_INVALID;
    (void) _anjay_find_server_iid(server->anjay, server->ssid, &server_iid);
    int64_t result;

    if (server_iid != ANJAY_ID_INVALID
            && !_anjay_dm_read_resource_i64(
                       server->anjay,
                       &MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid,
                                           rid),
                       &result)
            && result >= min_value && result <= UINT32_MAX) {
        *out_result = (uint32_t) result;
    }
}

typedef struct {
    uint32_t retry_count;
    uint32_t retry_timer_s;
    uint32_t sequence_retry_count;
    uint32_t sequence_delay_timer_s;
} communication_retry_params_t;

// NOTE: See "Table: 6.2.1.1.-1 Registration Procedures Default Values", it's
// where the default values are taken from.
static const communication_retry_params_t COMMUNICATION_RETRY_PARAMS_DEFAULT = {
    .retry_count = 5,
    .retry_timer_s = 60,
    .sequence_retry_count = 1,
    .sequence_delay_timer_s = 86400
};

static communication_retry_params_t
query_server_communication_retry_params(anjay_server_info_t *server) {
    assert(server->ssid != ANJAY_SSID_BOOTSTRAP);
    communication_retry_params_t params = COMMUNICATION_RETRY_PARAMS_DEFAULT;
    try_read_server_resource_u32(server,
                                 ANJAY_DM_RID_SERVER_COMMUNICATION_RETRY_COUNT,
                                 1, &params.retry_count);
    try_read_server_resource_u32(server,
                                 ANJAY_DM_RID_SERVER_COMMUNICATION_RETRY_TIMER,
                                 0, &params.retry_timer_s);
    try_read_server_resource_u32(
            server, ANJAY_DM_RID_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT, 0,
            &params.sequence_retry_count);
    try_read_server_resource_u32(
            server, ANJAY_DM_RID_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER, 0,
            &params.sequence_delay_timer_s);
    return params;
}
#endif // ANJAY_WITH_LWM2M11

void _anjay_server_on_failure(anjay_server_info_t *server,
                              const char *debug_msg) {
    // defined(ANJAY_WITH_CORE_PERSISTENCE)
    _anjay_server_clean_active_data(server);
    server->refresh_failed = true;

    if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        anjay_log(DEBUG,
                  _("Bootstrap Server: ") "%s" _(
                          ". Disabling it indefinitely."),
                  debug_msg);
        // Abort any further bootstrap retries.
        _anjay_bootstrap_cleanup(server->anjay);
    } else {
#ifdef ANJAY_WITH_LWM2M11
        if (server->registration_attempts > 0) {
            // When this value is > 0, it means there is an ongoing registration
            // sequence. Otherwise, this whole function was called due to some
            // other communication failure.
            const communication_retry_params_t params =
                    query_server_communication_retry_params(server);
            if (server->registration_attempts < params.retry_count) {
                anjay_log(INFO, _("Registration Retry ") "%u/%u",
                          server->registration_attempts,
                          params.retry_count - 1);

                const avs_time_duration_t retry_timer = avs_time_duration_mul(
                        avs_time_duration_from_scalar(params.retry_timer_s,
                                                      AVS_TIME_S),
                        (1 << (server->registration_attempts - 1)));
                if (!avs_time_duration_valid(retry_timer)) {
                    anjay_log(WARNING, _("Calculated retry time overflowed. "
                                         "Assuming infinity"));
                }
                server->reactivate_time =
                        avs_time_real_add(avs_time_real_now(), retry_timer);
                deactivate_server(server);
                return;
            } else if (server->registration_sequences_performed + 1
                       < params.sequence_retry_count) {
                anjay_log(INFO, _("Sequence Retry ") "%u/%u",
                          server->registration_sequences_performed + 1,
                          params.sequence_retry_count - 1);

                ++server->registration_sequences_performed;
                server->registration_attempts = 0;
                avs_time_duration_t disable_duration =
                        avs_time_duration_from_scalar(
                                params.sequence_delay_timer_s, AVS_TIME_S);
                if (params.sequence_delay_timer_s == UINT32_MAX) {
                    // E.2 LwM2M Object: LwM2M Server: "MAX_VALUE means do not
                    // perform another communication sequence."
                    anjay_log(INFO,
                              _("Communication Sequence Delay Timer is "
                                "saturated. Disabling server ") "%" PRIu16
                                      _(" indefinitely."),
                              server->ssid);
                    disable_duration = AVS_TIME_DURATION_INVALID;
                }
                server->reactivate_time = avs_time_real_add(avs_time_real_now(),
                                                            disable_duration);
                deactivate_server(server);
                return;
            }
        }
#endif // ANJAY_WITH_LWM2M11
       // Either a failure not due to registration, or the number of
       // registration attempts already exceeded communication retry counter.
        anjay_log(DEBUG,
                  _("Non-Bootstrap Server ") "%" PRIu16 _(": ") "%s" _("."),
                  server->ssid, debug_msg);
        (void) _anjay_perform_bootstrap_action_if_appropriate(
                server->anjay,
                _anjay_servers_find_active(server->anjay, ANJAY_SSID_BOOTSTRAP),
                _anjay_requested_bootstrap_action(server->anjay));
    }
    // make sure that the server will not be reactivated at next refresh
    server->reactivate_time = AVS_TIME_REAL_INVALID;
}

void _anjay_server_on_server_communication_error(anjay_server_info_t *server,
                                                 avs_error_t err) {
    assert(avs_is_err(err));
    if (_anjay_server_reschedule_next_action(
                server, AVS_TIME_DURATION_ZERO,
                ANJAY_SERVER_NEXT_ACTION_COMMUNICATION_ERROR)) {
        anjay_log(ERROR, _("could not schedule "
                           "ANJAY_SERVER_NEXT_ACTION_COMMUNICATION_ERROR"));
        server->refresh_failed = true;
    }
#ifdef ANJAY_WITH_LWM2M11
    if (err.category == AVS_NET_SSL_ALERT_CATEGORY) {
        _anjay_server_update_last_ssl_alert_code(
                server, avs_net_ssl_alert_level(err),
                avs_net_ssl_alert_description(err));
    }
#endif // ANJAY_WITH_LWM2M11
}

void _anjay_server_on_server_communication_timeout(
        anjay_server_info_t *server) {
    anjay_connection_ref_t ref = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_PRIMARY
    };
    assert(server);
    assert(ref.conn_type != ANJAY_CONNECTION_UNSET);
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    if (connection->state == ANJAY_SERVER_CONNECTION_STABLE
            && connection->stateful
            && !_anjay_schedule_disable_server_with_explicit_timeout_unlocked(
                       server->anjay, server->ssid, AVS_TIME_DURATION_ZERO)) {
        server->refresh_failed = true;
    } else {
        _anjay_server_on_server_communication_error(server,
                                                    avs_errno(AVS_EBADF));
    }
}

void _anjay_server_on_fatal_coap_error(anjay_connection_ref_t conn_ref,
                                       avs_error_t err) {
    assert(avs_coap_error_recovery_action(err)
           == AVS_COAP_ERR_RECOVERY_RECREATE_CONTEXT);
    anjay_server_connection_t *conn =
            _anjay_connection_get(&conn_ref.server->connections,
                                  conn_ref.conn_type);
    if (conn_ref.conn_type == ANJAY_CONNECTION_PRIMARY
            && conn->state != ANJAY_SERVER_CONNECTION_STABLE
            && _anjay_server_registration_expired(conn_ref.server)) {
        _anjay_server_on_server_communication_error(conn_ref.server, err);
    } else {
        // defined(ANJAY_WITH_CORE_PERSISTENCE)
        _anjay_connection_internal_clean_socket(conn_ref.server->anjay, conn);
        _anjay_active_server_refresh(conn_ref.server);
    }
}

void _anjay_server_on_refreshed(anjay_server_info_t *server,
                                anjay_server_connection_state_t state,
                                avs_error_t err) {
    assert(server);
    anjay_connection_ref_t primary_ref = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_PRIMARY
    };
    anjay_server_connection_t *primary_conn =
            _anjay_get_server_connection(primary_ref);
    if (state == ANJAY_SERVER_CONNECTION_OFFLINE) {
        if (avs_is_err(err)) {
            anjay_log(TRACE, _("could not initialize sockets for SSID ") "%u",
                      server->ssid);
            _anjay_server_on_server_communication_error(server, err);
        } else if (_anjay_socket_transport_supported(server->anjay,
                                                     primary_conn->transport)
                   && _anjay_socket_transport_is_online(
                              server->anjay, primary_conn->transport)) {
            assert(server->registration_info.queue_mode);
            anjay_log(TRACE,
                      _("Server with SSID ") "%u" _(
                              " is suspended due to queue mode"),
                      server->ssid);
            _anjay_server_reschedule_update_job(server);
        } else {
            anjay_log(TRACE, _("Server with SSID ") "%u" _(" is offline"),
                      server->ssid);
            if (!avs_time_real_valid(server->reactivate_time)) {
                // make the server reactivate when it comes back online
                server->reactivate_time = avs_time_real_now();
            }
        }
    } else if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        assert(avs_is_ok(err));
        anjay_bootstrap_action_t action =
                _anjay_requested_bootstrap_action(server->anjay);
        server->refresh_failed =
                !!_anjay_perform_bootstrap_action_if_appropriate(
                        server->anjay, server, action);
        if (action == ANJAY_BOOTSTRAP_ACTION_NONE) {
            _anjay_connection_mark_stable(primary_ref);
        }
        if (!server->refresh_failed) {
            server->reactivate_time = AVS_TIME_REAL_INVALID;
        }
        // _anjay_bootstrap_request_if_appropriate() may fail only due to
        // failure to schedule a job. Not much that we can do about it then.
    } else {
        assert(avs_is_ok(err));
        _anjay_server_ensure_valid_registration(server);
    }
}

void _anjay_server_on_updated_registration(anjay_server_info_t *server,
                                           anjay_registration_result_t result,
                                           avs_error_t err) {
    if (result == ANJAY_REGISTRATION_SUCCESS) {
        if (_anjay_server_reschedule_update_job(server)) {
            // Updates are retryable, we only need to reschedule after success
            result = ANJAY_REGISTRATION_ERROR_OTHER;
        } else {
            server->registration_attempts = 0;
            server->registration_sequences_performed = 0;
        }
    }
    switch (result) {
    case ANJAY_REGISTRATION_SUCCESS:
        server->reactivate_time = AVS_TIME_REAL_INVALID;
        server->refresh_failed = false;
#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
        server->registration_info.last_registration_time = avs_time_real_now();
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
       // Failure to handle Bootstrap state is not a failure of the
       // Register operation - hence, not checking return value.
        _anjay_bootstrap_notify_regular_connection_available(server->anjay);
        _anjay_connections_flush_notifications(&server->connections);
#ifdef ANJAY_WITH_SEND
        _anjay_send_sched_retry_deferred(server->anjay, server->ssid);
#endif // ANJAY_WITH_SEND
        break;
    case ANJAY_REGISTRATION_ERROR_TIMEOUT:
        _anjay_server_on_server_communication_timeout(server);
        break;
    default:
        _anjay_server_on_server_communication_error(
                server, avs_is_err(err) ? err : avs_errno(AVS_EPROTO));
        break;
    }
}

#if defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_BOOTSTRAP)
static bool
server_bootstrap_on_registration_failure(anjay_unlocked_t *anjay,
                                         anjay_server_info_t *server) {
    if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        return false;
    }
    // See "Table: 6.2.1.1.-1 Registration Procedures Default Values"
    bool force_bootstrap = true;

    anjay_iid_t server_iid = ANJAY_ID_INVALID;
    (void) _anjay_find_server_iid(anjay, server->ssid, &server_iid);

    if (server_iid != ANJAY_ID_INVALID) {
        (void) _anjay_dm_read_resource_bool(
                anjay,
                &MAKE_RESOURCE_PATH(
                        ANJAY_DM_OID_SERVER, server_iid,
                        ANJAY_DM_RID_SERVER_BOOTSTRAP_ON_REGISTRATION_FAILURE),
                &force_bootstrap);
    }
    return force_bootstrap;
}
#endif // defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_BOOTSTRAP)

#ifdef ANJAY_WITH_BOOTSTRAP
static bool should_retry_bootstrap(anjay_unlocked_t *anjay) {
    if (anjay->bootstrap.bootstrap_trigger) {
        return true;
    }
    bool bootstrap_server_exists = false;
    bool possibly_active_server_exists = false;
    bool registration_failure_must_trigger_bootstrap = false;
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers) {
        if (it->ssid == ANJAY_SSID_BOOTSTRAP) {
            if (anjay->bootstrap.in_progress) {
                // Bootstrap already in progress, there may be no need to retry
                return !_anjay_conn_session_tokens_equal(
                        anjay->bootstrap.bootstrap_session_token,
                        _anjay_server_primary_session_token(it));
            }
            bootstrap_server_exists = true;
        } else if (!it->refresh_failed || _anjay_server_active(it)) {
            possibly_active_server_exists = true;
        }
#    if defined(ANJAY_WITH_LWM2M11)
        else if (!registration_failure_must_trigger_bootstrap
                 && server_bootstrap_on_registration_failure(anjay, it)) {
            registration_failure_must_trigger_bootstrap = true;
        }
#    endif // defined(ANJAY_WITH_LWM2M11)
    }
    return bootstrap_server_exists
           && (!possibly_active_server_exists
               || registration_failure_must_trigger_bootstrap);
}
#endif // ANJAY_WITH_BOOTSTRAP

anjay_bootstrap_action_t
_anjay_requested_bootstrap_action(anjay_unlocked_t *anjay) {
#ifdef ANJAY_WITH_BOOTSTRAP
    // if Bootstrap attempt is already ongoing, there's no need to do anything
    if (!avs_coap_exchange_id_valid(
                anjay->bootstrap.outgoing_request_exchange_id)) {
        if (should_retry_bootstrap(anjay)) {
            return ANJAY_BOOTSTRAP_ACTION_REQUEST;
        }
    }
#endif // ANJAY_WITH_BOOTSTRAP
    (void) anjay;
    return ANJAY_BOOTSTRAP_ACTION_NONE;
}

/**
 * Checks whether all servers are inactive and have reached the limit of ICMP
 * failures (see the activation flow described in
 * _anjay_schedule_reload_servers() docs for details).
 */
bool anjay_all_connections_failed(anjay_t *anjay_locked) {
    bool result = false;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    if (anjay->servers) {
        result = true;
        AVS_LIST(anjay_server_info_t) it;
        AVS_LIST_FOREACH(it, anjay->servers) {
            if (_anjay_server_active(it) || !it->refresh_failed) {
                result = false;
                break;
            }
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int _anjay_server_sched_activate(anjay_server_info_t *server) {
    // start the backoff procedure from the beginning
    assert(!_anjay_server_active(server));
    assert(avs_time_real_valid(server->reactivate_time));
    server->refresh_failed = false;
    if (_anjay_server_is_disable_scheduled(server)) {
        // Server is in the process of being disabled. Let it happen, it will be
        // re-enabled afterwards, but make sure that reactivate_time is honored.
        server->next_action =
                ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_EXPLICIT_TIMEOUT;
        return 0;
    } else {
        return _anjay_schedule_refresh_server(
                server, avs_time_real_diff(server->reactivate_time,
                                           avs_time_real_now()));
    }
}

int _anjay_servers_sched_reactivate_all_given_up(anjay_unlocked_t *anjay) {
    int result = 0;
    bool active_server_exists = false;
    anjay_server_info_t *bootstrap_server = NULL;

    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers) {
        if (_anjay_server_active(it) || !it->refresh_failed) {
            active_server_exists = true;
            continue;
        }
        if (it->ssid == ANJAY_SSID_BOOTSTRAP) {
            bootstrap_server = it;
            if (!_anjay_bootstrap_legacy_server_initiated_allowed(anjay)) {
                continue;
            }
        }
        it->reactivate_time = avs_time_real_now();
        it->registration_attempts = 0;
        it->registration_sequences_performed = 0;
        if (!_anjay_server_sched_activate(it)) {
            active_server_exists = true;
        } else {
            result = -1;
        }
    }

    // if legacy Server-Initiated Bootstrap is not allowed and no other servers
    // exist, we want to reconnect the Bootstrap Server connection anyway.
    if (!active_server_exists && bootstrap_server) {
        assert(!_anjay_server_active(bootstrap_server));
        assert(bootstrap_server->refresh_failed);
        bootstrap_server->reactivate_time = avs_time_real_now();
        if (_anjay_server_sched_activate(bootstrap_server)) {
            result = -1;
        }
    }

    return result;
}

void _anjay_servers_add(AVS_LIST(anjay_server_info_t) *servers,
                        AVS_LIST(anjay_server_info_t) server) {
    assert(AVS_LIST_SIZE(server) == 1);
    AVS_LIST(anjay_server_info_t) *insert_ptr =
            _anjay_servers_find_insert_ptr(servers, server->ssid);

    assert(insert_ptr);
    AVS_ASSERT((!*insert_ptr || (*insert_ptr)->ssid != server->ssid),
               "attempting to insert a duplicate of an already existing server "
               "entry");

    AVS_LIST_INSERT(insert_ptr, server);
}

AVS_LIST(anjay_server_info_t)
_anjay_servers_create_inactive(anjay_unlocked_t *anjay, anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) new_server =
            AVS_LIST_NEW_ELEMENT(anjay_server_info_t);
    if (!new_server) {
        anjay_log(ERROR, _("out of memory"));
        return NULL;
    }

    new_server->anjay = anjay;
    new_server->ssid = ssid;
    new_server->last_used_security_iid = ANJAY_ID_INVALID;
    _anjay_connection_get(&new_server->connections, ANJAY_CONNECTION_PRIMARY)
            ->transport = ANJAY_SOCKET_TRANSPORT_INVALID;
    new_server->reactivate_time = AVS_TIME_REAL_INVALID;
    new_server->registration_info.lwm2m_version =
#ifdef ANJAY_WITH_LWM2M11
            anjay->lwm2m_version_config.maximum_version;
#else  // ANJAY_WITH_LWM2M11
            ANJAY_LWM2M_VERSION_1_0;
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    new_server->registration_info.last_registration_time =
            AVS_TIME_REAL_INVALID;
    new_server->last_communication_time = AVS_TIME_REAL_INVALID;
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    return new_server;
}

void _anjay_disable_server_with_timeout_from_dm_sync(
        anjay_server_info_t *server) {
    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(server->anjay, server->ssid, &server_iid)) {
        anjay_log(DEBUG,
                  _("no Server Object Instance with SSID = ") "%u" _(
                          ", disabling skipped"),
                  server->ssid);
    } else {
        const avs_time_duration_t disable_timeout =
                _anjay_disable_timeout_from_server_iid(server->anjay,
                                                       server_iid);
        server->reactivate_time =
                avs_time_real_add(avs_time_real_now(), disable_timeout);
        deactivate_server(server);
    }
}

static int disable_server_impl(anjay_unlocked_t *anjay,
                               anjay_ssid_t ssid,
                               anjay_server_next_action_t disable_action,
                               const char *disable_action_str,
                               avs_time_duration_t timeout) {
    assert(disable_action
                   == ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_TIMEOUT_FROM_DM
           || disable_action
                      == ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_EXPLICIT_TIMEOUT);
    if (ssid == ANJAY_SSID_ANY) {
        anjay_log(WARNING, _("invalid SSID: ") "%u", ssid);
        return -1;
    }

    anjay_server_info_t *server = _anjay_servers_find(anjay, ssid);
    if (!server) {
        return -1;
    }

    bool server_active = _anjay_server_active(server);
    if (!server_active && server->next_action_handle
            && server->next_action
                           == ANJAY_SERVER_NEXT_ACTION_COMMUNICATION_ERROR) {
        return -1;
    }

    if (_anjay_server_reschedule_next_action(server, AVS_TIME_DURATION_ZERO,
                                             disable_action)) {
        anjay_log(ERROR, _("could not schedule ") "%s", disable_action_str);
        return -1;
    }

    if (disable_action
            == ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_EXPLICIT_TIMEOUT) {
        server->reactivate_time =
                avs_time_real_add(avs_time_real_now(), timeout);
    }
    // defined(ANJAY_WITH_CORE_PERSISTENCE)
    return 0;
}

/**
 * Disables a specified server - in a scheduler job which calls
 * deactivate_server(). The reactivation timeout is read from data model.
 * See the documentation of _anjay_schedule_reload_servers() for details on how
 * does the deactivation procedure work.
 */
int anjay_disable_server(anjay_t *anjay_locked, anjay_ssid_t ssid) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = disable_server_impl(
            anjay, ssid, ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_TIMEOUT_FROM_DM,
            "ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_TIMEOUT_FROM_DM",
            AVS_TIME_DURATION_INVALID);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

void _anjay_disable_server_with_explicit_timeout_sync(
        anjay_server_info_t *server) {
    if (deactivate_server(server)) {
        anjay_log(ERROR, _("unable to deactivate server: ") "%" PRIu16,
                  server->ssid);
    } else {
        if (avs_time_real_valid(server->reactivate_time)) {
            anjay_log(INFO, _("server ") "%" PRIu16 _(" disabled for ") "%s",
                      server->ssid,
                      AVS_TIME_DURATION_AS_STRING(avs_time_real_diff(
                              server->reactivate_time, avs_time_real_now())));
        } else {
            anjay_log(INFO, _("server ") "%" PRIu16 _(" disabled"),
                      server->ssid);
        }
    }
}

int _anjay_schedule_disable_server_with_explicit_timeout_unlocked(
        anjay_unlocked_t *anjay,
        anjay_ssid_t ssid,
        avs_time_duration_t timeout) {
    return disable_server_impl(
            anjay, ssid, ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_EXPLICIT_TIMEOUT,
            "ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_EXPLICIT_TIMEOUT", timeout);
}

int anjay_disable_server_with_timeout(anjay_t *anjay_locked,
                                      anjay_ssid_t ssid,
                                      avs_time_duration_t timeout) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = _anjay_schedule_disable_server_with_explicit_timeout_unlocked(
            anjay, ssid, timeout);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int _anjay_enable_server_unlocked(anjay_unlocked_t *anjay, anjay_ssid_t ssid) {
    if (ssid == ANJAY_SSID_ANY) {
        anjay_log(WARNING, _("invalid SSID: ") "%u", ssid);
        return -1;
    }

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(&anjay->servers, ssid);

    if (!server_ptr || !*server_ptr || _anjay_server_active(*server_ptr)) {
        anjay_log(TRACE, _("not an inactive server: SSID = ") "%u", ssid);
        return -1;
    }

    if (ssid == ANJAY_SSID_BOOTSTRAP
            && !_anjay_bootstrap_legacy_server_initiated_allowed(anjay)
            && _anjay_requested_bootstrap_action(anjay)
                           == ANJAY_BOOTSTRAP_ACTION_NONE) {
        anjay_log(DEBUG,
                  _("1.0-style Server-Initiated Bootstrap is disabled and "
                    "Client - Initiated Bootstrap is currently not allowed, "
                    "not enabling Bootstrap Server"));
        return -1;
    }

    (*server_ptr)->reactivate_time = avs_time_real_now();
    return _anjay_server_sched_activate(*server_ptr);
}

int anjay_enable_server(anjay_t *anjay_locked, anjay_ssid_t ssid) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = _anjay_enable_server_unlocked(anjay, ssid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}
