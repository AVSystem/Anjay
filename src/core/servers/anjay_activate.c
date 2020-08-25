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

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_sched.h>

#include <inttypes.h>

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_servers_inactive.h"
#include "../anjay_servers_utils.h"
#include "../dm/anjay_query.h"

#include "anjay_activate.h"
#include "anjay_register.h"
#include "anjay_reload.h"
#include "anjay_server_connections.h"
#include "anjay_servers_internal.h"

VISIBILITY_SOURCE_BEGIN

void _anjay_server_on_failure(anjay_server_info_t *server,
                              const char *debug_msg) {
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

static void server_communication_error_job(avs_sched_t *sched,
                                           const void *server_ptr) {
    (void) sched;
    _anjay_server_on_failure(*(anjay_server_info_t *const *) server_ptr,
                             "not reachable");
}

void _anjay_server_on_server_communication_error(anjay_server_info_t *server,
                                                 avs_error_t err) {
    assert(avs_is_err(err));
    avs_sched_del(&server->next_action_handle);
    if (AVS_SCHED_NOW(server->anjay->sched, &server->next_action_handle,
                      server_communication_error_job, &server,
                      sizeof(server))) {
        anjay_log(ERROR,
                  _("could not schedule server_communication_error_job"));
        server->refresh_failed = true;
    }
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
            && !anjay_disable_server_with_timeout(server->anjay, server->ssid,
                                                  AVS_TIME_DURATION_ZERO)) {
        server->refresh_failed = true;
    } else {
        _anjay_server_on_server_communication_error(server,
                                                    avs_errno(AVS_EBADF));
    }
}

void _anjay_server_on_fatal_coap_error(anjay_connection_ref_t conn_ref) {
    anjay_server_connection_t *conn =
            _anjay_connection_get(&conn_ref.server->connections,
                                  conn_ref.conn_type);
    _anjay_connection_internal_clean_socket(conn_ref.server->anjay, conn);
    _anjay_active_server_refresh(conn_ref.server);
}

void _anjay_server_on_refreshed(anjay_server_info_t *server,
                                anjay_server_connection_state_t state,
                                avs_error_t err) {
    assert(server);
    if (state == ANJAY_SERVER_CONNECTION_OFFLINE) {
        if (avs_is_err(err)) {
            anjay_log(TRACE, _("could not initialize sockets for SSID ") "%u",
                      server->ssid);
            _anjay_server_on_server_communication_error(server, err);
        } else {
            anjay_log(TRACE, _("Server with SSID ") "%u" _(" is offline"),
                      server->ssid);
            if (!avs_time_real_valid(server->reactivate_time)) {
                // make the server reactive when it comes back online
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
            _anjay_connection_mark_stable((anjay_connection_ref_t) {
                .server = server,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
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
        }
    }
    switch (result) {
    case ANJAY_REGISTRATION_SUCCESS:
        server->reactivate_time = AVS_TIME_REAL_INVALID;
        server->refresh_failed = false;
        // Failure to handle Bootstrap state is not a failure of the
        // Register operation - hence, not checking return value.
        _anjay_bootstrap_notify_regular_connection_available(server->anjay);
        _anjay_connections_flush_notifications(&server->connections);
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

#ifdef ANJAY_WITH_BOOTSTRAP
static bool should_retry_bootstrap(anjay_t *anjay) {
    if (anjay->bootstrap.bootstrap_trigger) {
        return true;
    }
    bool bootstrap_server_exists = false;
    bool possibly_active_server_exists = false;
    bool registration_failure_must_trigger_bootstrap = false;
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
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
    }
    return bootstrap_server_exists
           && (!possibly_active_server_exists
               || registration_failure_must_trigger_bootstrap);
}
#endif // ANJAY_WITH_BOOTSTRAP

anjay_bootstrap_action_t _anjay_requested_bootstrap_action(anjay_t *anjay) {
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
bool anjay_all_connections_failed(anjay_t *anjay) {
    if (!anjay->servers->servers) {
        return false;
    }
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it) || !it->refresh_failed) {
            return false;
        }
    }
    return true;
}

int _anjay_server_sched_activate(anjay_server_info_t *server,
                                 avs_time_duration_t reactivate_delay) {
    // start the backoff procedure from the beginning
    assert(!_anjay_server_active(server));
    server->reactivate_time =
            avs_time_real_add(avs_time_real_now(), reactivate_delay);
    server->refresh_failed = false;
    return _anjay_schedule_refresh_server(server, reactivate_delay);
}

int _anjay_servers_sched_reactivate_all_given_up(anjay_t *anjay) {
    int result = 0;

    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it) || !it->refresh_failed
                || (it->ssid == ANJAY_SSID_BOOTSTRAP
                    && !_anjay_bootstrap_legacy_server_initiated_allowed(
                               anjay))) {
            continue;
        }
        int partial = _anjay_server_sched_activate(it, AVS_TIME_DURATION_ZERO);
        if (!result) {
            result = partial;
        }
    }

    return result;
}

void _anjay_servers_add(anjay_servers_t *servers,
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

int _anjay_server_deactivate(anjay_t *anjay,
                             anjay_ssid_t ssid,
                             avs_time_duration_t reactivate_delay) {
    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(anjay->servers, ssid);
    if (!server_ptr) {
        anjay_log(ERROR, _("SSID ") "%" PRIu16 _(" is not a known server"),
                  ssid);
        return -1;
    }

#ifndef ANJAY_WITHOUT_DEREGISTER
    if (ssid != ANJAY_SSID_BOOTSTRAP && !_anjay_bootstrap_in_progress(anjay)
            && _anjay_server_active(*server_ptr)
            && !_anjay_server_registration_expired(*server_ptr)) {
        // Return value intentionally ignored.
        // There isn't much we can do in case it fails and De-Register is
        // optional anyway. _anjay_serve_deregister logs the error cause.
        _anjay_server_deregister(*server_ptr);
    }
#endif // ANJAY_WITHOUT_DEREGISTER
    _anjay_server_clean_active_data(*server_ptr);
    (*server_ptr)->registration_info.expire_time = AVS_TIME_REAL_INVALID;
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        _anjay_connection_internal_invalidate_session(
                _anjay_connection_get(&(*server_ptr)->connections, conn_type));
    }
    if (avs_time_duration_valid(reactivate_delay)
            && _anjay_server_sched_activate(*server_ptr, reactivate_delay)) {
        // not much we can do other than removing the server altogether
        anjay_log(ERROR, _("could not reschedule server reactivation"));
        AVS_LIST_DELETE(server_ptr);
        return -1;
    }
    return 0;
}

AVS_LIST(anjay_server_info_t)
_anjay_servers_create_inactive(anjay_t *anjay, anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) new_server =
            AVS_LIST_NEW_ELEMENT(anjay_server_info_t);
    if (!new_server) {
        anjay_log(ERROR, _("out of memory"));
        return NULL;
    }

    new_server->anjay = anjay;
    new_server->ssid = ssid;
    new_server->last_used_security_iid = ANJAY_ID_INVALID;
    new_server->reactivate_time = AVS_TIME_REAL_INVALID;
    new_server->registration_info.lwm2m_version = ANJAY_LWM2M_VERSION_1_0;
    return new_server;
}

static void disable_server_job(avs_sched_t *sched, const void *ssid_ptr) {
    anjay_t *anjay = _anjay_get_from_sched(sched);
    anjay_ssid_t ssid = *(const anjay_ssid_t *) ssid_ptr;

    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        anjay_log(DEBUG,
                  _("no Server Object Instance with SSID = ") "%u" _(
                          ", disabling skipped"),
                  ssid);
    } else {
        const avs_time_duration_t disable_timeout =
                _anjay_disable_timeout_from_server_iid(anjay, server_iid);
        _anjay_server_deactivate(anjay, ssid, disable_timeout);
    }
}

/**
 * Disables a specified server - in a scheduler job which calls
 * _anjay_server_deactivate(). The reactivation timeout is read from data model.
 * See the documentation of _anjay_schedule_reload_servers() for details on how
 * does the deactivation procedure work.
 */
int anjay_disable_server(anjay_t *anjay, anjay_ssid_t ssid) {
    anjay_server_info_t *server = _anjay_servers_find_active(anjay, ssid);
    if (!server) {
        return -1;
    }

    avs_sched_del(&server->next_action_handle);
    if (AVS_SCHED_NOW(anjay->sched, &server->next_action_handle,
                      disable_server_job, &ssid, sizeof(ssid))) {
        anjay_log(ERROR, _("could not schedule disable_server_job"));
        return -1;
    }

    return 0;
}

typedef struct {
    anjay_ssid_t ssid;
    avs_time_duration_t timeout;
} disable_server_data_t;

static void disable_server_with_timeout_job(avs_sched_t *sched,
                                            const void *data_ptr_) {
    const disable_server_data_t *data =
            (const disable_server_data_t *) data_ptr_;
    if (_anjay_server_deactivate(_anjay_get_from_sched(sched), data->ssid,
                                 data->timeout)) {
        anjay_log(ERROR, _("unable to deactivate server: ") "%" PRIu16,
                  data->ssid);
    } else {
        if (avs_time_duration_valid(data->timeout)) {
            anjay_log(INFO, _("server ") "%" PRIu16 _(" disabled for ") "%s",
                      data->ssid, AVS_TIME_DURATION_AS_STRING(data->timeout));
        } else {
            anjay_log(INFO, _("server ") "%" PRIu16 _(" disabled"), data->ssid);
        }
    }
}

/**
 * Basically the same as anjay_disable_server(), but with explicit timeout value
 * instead of reading it from the data model.
 *
 * Aside from being a public API, it is called from:
 *
 * - bootstrap_finish_impl(), to deactivate the Bootstrap Server connection if
 *   legacy Server-Initiated Bootstrap is disabled
 * - serv_execute(), as a reference implementation of the Disable resource
 * - _anjay_schedule_socket_update(), to force reconnection of all sockets
 */
int anjay_disable_server_with_timeout(anjay_t *anjay,
                                      anjay_ssid_t ssid,
                                      avs_time_duration_t timeout) {
    if (ssid == ANJAY_SSID_ANY) {
        anjay_log(WARNING, _("invalid SSID: ") "%u", ssid);
        return -1;
    }

    anjay_server_info_t *server = _anjay_servers_find_active(anjay, ssid);
    if (!server) {
        return -1;
    }

    disable_server_data_t data = {
        .ssid = ssid,
        .timeout = timeout
    };

    avs_sched_del(&server->next_action_handle);
    if (AVS_SCHED_NOW(anjay->sched, &server->next_action_handle,
                      disable_server_with_timeout_job, &data, sizeof(data))) {
        anjay_log(ERROR,
                  _("could not schedule disable_server_with_timeout_job"));
        return -1;
    }

    return 0;
}

/**
 * Schedules server activation immediately, after some sanity checks.
 *
 * The activation request is rejected if someone tries to enable the Bootstrap
 * Server, Client-Initiated Bootstrap is not supposed to be performed, and
 * legacy Server-Initiated Bootstrap is administratively disabled.
 */
int anjay_enable_server(anjay_t *anjay, anjay_ssid_t ssid) {
    if (ssid == ANJAY_SSID_ANY) {
        anjay_log(WARNING, _("invalid SSID: ") "%u", ssid);
        return -1;
    }

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(anjay->servers, ssid);

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

    return _anjay_server_sched_activate(*server_ptr, AVS_TIME_DURATION_ZERO);
}
