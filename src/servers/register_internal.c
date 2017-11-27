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

#include <inttypes.h>

#include <anjay_modules/time_defs.h>

#include "../anjay_core.h"
#include "../servers.h"
#include "../interface/register.h"

#define ANJAY_SERVERS_INTERNALS

#include "activate.h"
#include "connection_info.h"
#include "register_internal.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

/** Update messages are sent to the server every
 * LIFETIME/ANJAY_UPDATE_INTERVAL_FACTOR seconds. */
#define ANJAY_UPDATE_INTERVAL_MARGIN_FACTOR 2

/** To avoid flooding the network in case of a very small lifetime, Update
 * messages are not sent more often than every ANJAY_MIN_UPDATE_INTERVAL_S
 * seconds. */
#define ANJAY_MIN_UPDATE_INTERVAL_S 1

typedef enum {
    SOCKET_NEEDS_NOTHING = 0,
    SOCKET_NEEDS_RECONNECT,
    _SOCKET_NEEDS_LIMIT
} anjay_socket_needs_t;

#define SEND_UPDATE_SCHED_JOB_SOCKET_NEEDS_SHIFT 16
AVS_STATIC_ASSERT(
        ((_SOCKET_NEEDS_LIMIT - 1) << SEND_UPDATE_SCHED_JOB_SOCKET_NEEDS_SHIFT)
                < UINTPTR_MAX,
        pointer_big_enough);

static void *send_update_args_encode(anjay_ssid_t ssid,
                                     anjay_socket_needs_t socket_needs) {
    uintptr_t value =
            ((uintptr_t) socket_needs
                    << SEND_UPDATE_SCHED_JOB_SOCKET_NEEDS_SHIFT)
            | ssid;
    return (void *) value;
}

static void send_update_args_decode(anjay_ssid_t *out_ssid,
                                    anjay_socket_needs_t *out_socket_needs,
                                    void *value) {
    uintptr_t int_value = (uintptr_t) value;
    *out_ssid = (anjay_ssid_t) (int_value & UINT16_MAX);
    *out_socket_needs = (anjay_socket_needs_t)
            (int_value >> SEND_UPDATE_SCHED_JOB_SOCKET_NEEDS_SHIFT);
    assert(*out_socket_needs >= 0 && *out_socket_needs < _SOCKET_NEEDS_LIMIT);
}

static int send_update_sched_job(anjay_t *anjay, void *args) {
    anjay_ssid_t ssid;
    anjay_socket_needs_t socket_needs;
    send_update_args_decode(&ssid, &socket_needs, args);

    assert(ssid != ANJAY_SSID_ANY);

    AVS_LIST(anjay_active_server_info_t) server =
            _anjay_servers_find_active(&anjay->servers, ssid);

    if (!server) {
        return -1;
    }

    bool is_bootstrap = (server->ssid == ANJAY_SSID_BOOTSTRAP);

    int result = _anjay_server_refresh(anjay, server, socket_needs);
    if (!result && socket_needs != SOCKET_NEEDS_NOTHING && is_bootstrap) {
        result = _anjay_bootstrap_update_reconnected(anjay);
    }

    server_registration_operation_t attempted_operation;
    if (!result && !is_bootstrap) {
        if ((result = _anjay_server_update_or_reregister(
                anjay, server, &attempted_operation))) {
            if (attempted_operation == SERVER_REGISTRATION_RETRY) {
                anjay_log(DEBUG, "re-registration failed");
                // mark that the registration connection is no longer valid;
                // prevents superfluous Deregister
                server->registration_info.conn_type = ANJAY_CONNECTION_UNSET;
                _anjay_server_deactivate(anjay, &anjay->servers, ssid,
                                         AVS_TIME_DURATION_ZERO);
                return 0;
            } else if (result == AVS_COAP_CTX_ERR_NETWORK) {
                anjay_log(ERROR, "network communication error while updating "
                          "registration for SSID==%" PRIu16, server->ssid);
                // We cannot use _anjay_schedule_server_reconnect(), because it
                // would mean an endless loop without backoff if the server is
                // down. Instead, we disconnect the socket and rely on
                // scheduler's backoff. During the next call,
                // _anjay_server_refresh() will reconnect the socket.
                _anjay_connection_suspend((anjay_connection_ref_t) {
                    .server = server,
                    .conn_type = server->registration_info.conn_type
                });
            }
        } else {
            // Updates are retryable, we only need to reschedule after success
            result = _anjay_server_reschedule_update_job(anjay, server);
        }
    }
    return result;
}

static avs_time_duration_t
get_server_update_interval(const anjay_registration_info_t *info) {
    return avs_time_duration_div(
            avs_time_duration_from_scalar(info->last_update_params.lifetime_s,
                                          AVS_TIME_S),
            ANJAY_UPDATE_INTERVAL_MARGIN_FACTOR);
}

static int
schedule_update(anjay_t *anjay,
                anjay_sched_handle_t *out_handle,
                const anjay_active_server_info_t *server,
                avs_time_duration_t delay,
                anjay_socket_needs_t socket_needs) {
    anjay_log(DEBUG, "scheduling update for SSID %u after "
                     "%" PRId64 ".%09" PRId32,
              server->ssid, delay.seconds, delay.nanoseconds);

    void *update_args = send_update_args_encode(server->ssid, socket_needs);

    return _anjay_sched_retryable(anjay->sched, out_handle, delay,
                                  ANJAY_SERVER_RETRYABLE_BACKOFF,
                                  send_update_sched_job, update_args);
}

static int
schedule_next_update(anjay_t *anjay,
                     anjay_sched_handle_t *out_handle,
                     const anjay_active_server_info_t *server) {
    avs_time_duration_t remaining =
            _anjay_register_time_remaining(&server->registration_info);
    avs_time_duration_t update_interval =
            get_server_update_interval(&server->registration_info);
    remaining = avs_time_duration_diff(remaining, update_interval);

    if (remaining.seconds < ANJAY_MIN_UPDATE_INTERVAL_S) {
        remaining = avs_time_duration_from_scalar(ANJAY_MIN_UPDATE_INTERVAL_S,
                                                  AVS_TIME_S);
    }

    return schedule_update(anjay, out_handle, server, remaining,
                           SOCKET_NEEDS_NOTHING);
}

static int
send_update(anjay_t *anjay,
            anjay_active_server_info_t *server,
            server_registration_operation_t *out_attempted_operation) {
    *out_attempted_operation = SERVER_REGISTRATION_UPDATE;
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = server->registration_info.conn_type
    };
    if (_anjay_bind_server_stream(anjay, connection)) {
        anjay_log(ERROR, "could not get stream for server %u", server->ssid);
        return -1;
    }

    int result = _anjay_update_registration(anjay);
    if (result == 0) {
        _anjay_observe_sched_flush_current_connection(anjay);
    }

    avs_stream_reset(anjay->comm_stream);
    _anjay_release_server_stream(anjay);

    if (result == ANJAY_REGISTRATION_UPDATE_REJECTED) {
        anjay_log(DEBUG, "update rejected for SSID = %u; re-registering",
                  server->ssid);
        *out_attempted_operation = SERVER_REGISTRATION_RETRY;
        result = _anjay_server_register(anjay, server);
    } else if (result != 0) {
        anjay_log(ERROR, "could not send registration update: %d", result);
    }

    return result;
}

bool _anjay_server_registration_connection_valid(
        anjay_active_server_info_t *server) {
    return server->registration_info.conn_type != ANJAY_CONNECTION_UNSET
            && _anjay_connection_is_online(
                       (anjay_connection_ref_t) {
                           .server = server,
                           .conn_type = server->registration_info.conn_type
                       });
}

bool _anjay_server_registration_expired(anjay_active_server_info_t *server) {
    avs_time_duration_t remaining =
            _anjay_register_time_remaining(&server->registration_info);
    if (avs_time_duration_less(remaining, AVS_TIME_DURATION_ZERO)) {
        anjay_log(DEBUG, "Registration Lifetime expired for SSID = %u, "
                  "forcing re-register", server->ssid);
        return true;
    }
    return false;
}

int _anjay_server_update_or_reregister(
        anjay_t *anjay,
        anjay_active_server_info_t *server,
        server_registration_operation_t *out_attempted_operation) {
    *out_attempted_operation = SERVER_REGISTRATION_RETRY;

    if (_anjay_server_registration_connection_valid(server)) {
        if (!_anjay_server_registration_expired(server)) {
            return send_update(anjay, server, out_attempted_operation);
        }
    } else {
        anjay_log(INFO, "No valid existing connection to Registration Interface"
                  " for SSID = %u, re-registering", server->ssid);
        if (_anjay_server_setup_registration_connection(server)) {
            return -1;
        }
    }
    return _anjay_server_register(anjay, server);
}

int _anjay_server_reschedule_update_job(anjay_t *anjay,
                                        anjay_active_server_info_t *server) {
    _anjay_sched_del(anjay->sched, &server->sched_update_handle);
    if (schedule_next_update(anjay, &server->sched_update_handle, server)) {
        anjay_log(ERROR, "could not schedule next Update for server %u",
                  server->ssid);
        return -1;
    }
    return 0;
}

static int reschedule_update_for_server(anjay_t *anjay,
                                        anjay_active_server_info_t *server,
                                        anjay_socket_needs_t socket_needs) {
    _anjay_sched_del(anjay->sched, &server->sched_update_handle);
    if (schedule_update(anjay, &server->sched_update_handle, server,
                        AVS_TIME_DURATION_ZERO, socket_needs)) {
        anjay_log(ERROR, "could not schedule send_update_sched_job");
        return -1;
    }
    return 0;
}

static int
reschedule_update_for_all_servers(anjay_t *anjay,
                                  anjay_socket_needs_t socket_needs) {
    int result = 0;

    AVS_LIST(anjay_active_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers.active) {
        int partial = reschedule_update_for_server(anjay, it, socket_needs);
        if (!result) {
            result = partial;
        }
    }

    return result;
}

int anjay_schedule_registration_update(anjay_t *anjay,
                                       anjay_ssid_t ssid) {
    if (anjay_is_offline(anjay)) {
        anjay_log(ERROR,
                  "cannot schedule registration update while being offline");
        return -1;
    }
    int result = 0;

    if (ssid == ANJAY_SSID_ANY) {
        result = reschedule_update_for_all_servers(anjay, SOCKET_NEEDS_NOTHING);
    } else {
        anjay_active_server_info_t *server =
                _anjay_servers_find_active(&anjay->servers, ssid);
        if (!server) {
            anjay_log(ERROR, "no active server with SSID = %u", ssid);
            result = -1;
        } else {
            result = reschedule_update_for_server(anjay, server,
                                                  SOCKET_NEEDS_NOTHING);
        }
    }

    return result;
}

int anjay_schedule_reconnect(anjay_t *anjay) {
    int result = reschedule_update_for_all_servers(anjay,
                                                   SOCKET_NEEDS_RECONNECT);
    if (result) {
        return result;
    }
    anjay->offline = false;
    return 0;
}

int _anjay_schedule_server_reconnect(anjay_t *anjay,
                                     anjay_active_server_info_t *server) {
    return reschedule_update_for_server(anjay, server, SOCKET_NEEDS_RECONNECT);
}

int _anjay_server_register(anjay_t *anjay,
                           anjay_active_server_info_t *server) {
    if (_anjay_server_setup_registration_connection(server)) {
        return -1;
    }
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = server->registration_info.conn_type
    };
    if (_anjay_bind_server_stream(anjay, connection)) {
        return -1;
    }

    int result = _anjay_register(anjay);
    avs_stream_reset(anjay->comm_stream);

    if (!result) {
        _anjay_sched_del(anjay->sched, &server->sched_update_handle);
        if (schedule_next_update(anjay, &server->sched_update_handle, server)) {
            anjay_log(WARNING, "could not schedule Update for server %u",
                      server->ssid);
        }

        _anjay_observe_sched_flush_current_connection(anjay);
        _anjay_bootstrap_notify_regular_connection_available(anjay);
    }
    _anjay_release_server_stream(anjay);
    return result;
}

int _anjay_server_deregister(anjay_t *anjay,
                             anjay_active_server_info_t *server) {
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = server->registration_info.conn_type
    };
    if (connection.conn_type >= ANJAY_CONNECTION_UNSET
            || _anjay_bind_server_stream(anjay, connection)) {
        anjay_log(ERROR, "could not get stream for server %u, skipping",
                  server->ssid);
        return 0;
    }

    int result = _anjay_deregister(anjay);
    if (result) {
        anjay_log(ERROR, "could not send De-Register request: %d", result);
    }

    avs_stream_reset(anjay->comm_stream);
    _anjay_release_server_stream_without_scheduling_queue(anjay);
    return result;
}
