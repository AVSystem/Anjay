/*
 * Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_core.h"
#include "../servers.h"
#include "../interface/register.h"

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
    /* Some compilers implement anjay_socket_needs_t as unsigned int, causing
     * warning of pointless comparison. The solution is to make a temporary
     * int32_t variable with value of unit. */
    const int32_t s_out_socket_needs = *out_socket_needs;
    assert(s_out_socket_needs >= 0 && *out_socket_needs < _SOCKET_NEEDS_LIMIT);
}

static anjay_sched_retryable_result_t
send_update_sched_job(anjay_t *anjay, void *args) {
    anjay_ssid_t ssid;
    anjay_socket_needs_t socket_needs;
    send_update_args_decode(&ssid, &socket_needs, args);

    assert(ssid != ANJAY_SSID_ANY);

    AVS_LIST(anjay_server_info_t) server =
            _anjay_servers_find_active(anjay->servers, ssid);
    if (!server) {
        return ANJAY_SCHED_RETRY;
    }

    if (_anjay_active_server_refresh(anjay, server, socket_needs)) {
        if (!_anjay_server_registration_expired(server)) {
            return ANJAY_SCHED_RETRY;
        }
    } else if (ssid == ANJAY_SSID_BOOTSTRAP) {
        if (socket_needs == SOCKET_NEEDS_NOTHING
                || !_anjay_bootstrap_update_reconnected(anjay)) {
            return ANJAY_SCHED_FINISH;
        } else {
            return ANJAY_SCHED_RETRY;
        }
    } else {
        server->data_active.registration_info.needs_update = true;
        int result = _anjay_server_ensure_valid_registration(anjay, server);
        if (!result) {
            return ANJAY_SCHED_FINISH;
        } else if (result < 0) {
            return ANJAY_SCHED_RETRY;
        }
    }
    // mark that the registration connection is no longer valid;
    // prevents superfluous Deregister
    server->data_active.registration_info.conn_type = ANJAY_CONNECTION_UNSET;
    _anjay_server_deactivate(anjay, ssid, AVS_TIME_DURATION_ZERO);
    return ANJAY_SCHED_FINISH;
}

/**
 * Returns the duration that we should reserve before expiration of lifetime for
 * performing the Update operation.
 */
static avs_time_duration_t
get_server_update_interval_margin(anjay_t *anjay,
                                  const anjay_registration_info_t *info) {
    avs_time_duration_t half_lifetime =
        avs_time_duration_div(
                avs_time_duration_from_scalar(
                        info->last_update_params.lifetime_s, AVS_TIME_S),
                ANJAY_UPDATE_INTERVAL_MARGIN_FACTOR);
    avs_time_duration_t max_transmit_wait =
            avs_coap_max_transmit_wait(_anjay_tx_params_for_conn_type(
                    anjay, info->conn_type));
    if (avs_time_duration_less(half_lifetime, max_transmit_wait)) {
        return half_lifetime;
    } else {
        return max_transmit_wait;
    }
}

static int
schedule_update(anjay_t *anjay,
                anjay_sched_handle_t *out_handle,
                const anjay_server_info_t *server,
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
                     anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    avs_time_duration_t remaining = _anjay_register_time_remaining(
            &server->data_active.registration_info);
    avs_time_duration_t interval_margin = get_server_update_interval_margin(
            anjay, &server->data_active.registration_info);
    remaining = avs_time_duration_diff(remaining, interval_margin);

    if (remaining.seconds < ANJAY_MIN_UPDATE_INTERVAL_S) {
        remaining = avs_time_duration_from_scalar(ANJAY_MIN_UPDATE_INTERVAL_S,
                                                  AVS_TIME_S);
    }

    return schedule_update(anjay, out_handle, server, remaining,
                           SOCKET_NEEDS_NOTHING);
}

bool _anjay_server_registration_connection_valid(anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    return server->data_active.registration_info.conn_type
                    != ANJAY_CONNECTION_UNSET
            && _anjay_connection_get_online_socket(
                       (anjay_connection_ref_t) {
                           .server = server,
                        .conn_type =
                                server->data_active.registration_info.conn_type
                       }) != NULL;
}

bool _anjay_server_registration_expired(anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    avs_time_duration_t remaining = _anjay_register_time_remaining(
            &server->data_active.registration_info);
    if (avs_time_duration_less(remaining, AVS_TIME_DURATION_ZERO)) {
        anjay_log(DEBUG, "Registration Lifetime expired for SSID = %u, "
                  "forcing re-register", server->ssid);
        return true;
    }
    return false;
}

int _anjay_server_reschedule_update_job(anjay_t *anjay,
                                        anjay_server_info_t *server) {
    _anjay_sched_del(anjay->sched, &server->sched_update_or_reactivate_handle);
    if (schedule_next_update(anjay, &server->sched_update_or_reactivate_handle,
                             server)) {
        anjay_log(ERROR, "could not schedule next Update for server %u",
                  server->ssid);
        return -1;
    }
    return 0;
}

static int reschedule_update_for_server(anjay_t *anjay,
                                        anjay_server_info_t *server,
                                        anjay_socket_needs_t socket_needs) {
    _anjay_sched_del(anjay->sched, &server->sched_update_or_reactivate_handle);
    if (schedule_update(anjay, &server->sched_update_or_reactivate_handle,
                        server, AVS_TIME_DURATION_ZERO, socket_needs)) {
        anjay_log(ERROR, "could not schedule send_update_sched_job");
        return -1;
    }
    return 0;
}

static int
reschedule_update_for_all_servers(anjay_t *anjay,
                                  anjay_socket_needs_t socket_needs) {
    int result = 0;

    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it)) {
            int partial = reschedule_update_for_server(anjay, it, socket_needs);
            if (!result) {
                result = partial;
            }
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
        anjay_server_info_t *server =
                _anjay_servers_find_active(anjay->servers, ssid);
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
    if (!result) {
        result = _anjay_servers_sched_reactivate_all_given_up(anjay);
    }
#ifdef WITH_DOWNLOADER
    if (!result) {
        result = _anjay_downloader_sched_reconnect_all(&anjay->downloader);
    }
#endif // WITH_DOWNLOADER
    if (result) {
        return result;
    }
    anjay->offline = false;
    return 0;
}

int _anjay_schedule_reregister(anjay_t *anjay, anjay_server_info_t *server) {
    // mark that the registration connection is no longer valid;
    // forces re-register
    assert(_anjay_server_active(server));
    server->data_active.registration_info.conn_type = ANJAY_CONNECTION_UNSET;
    return anjay_schedule_registration_update(anjay, server->ssid);
}

int _anjay_schedule_server_reconnect(anjay_t *anjay,
                                     anjay_server_info_t *server) {
    return reschedule_update_for_server(anjay, server, SOCKET_NEEDS_RECONNECT);
}

static int try_register(anjay_t *anjay,
                        anjay_registration_update_ctx_t *ctx) {
    int retval = _anjay_register(ctx);
    if (retval) {
        anjay_log(DEBUG, "re-registration failed");
    } else {
        _anjay_bootstrap_notify_regular_connection_available(anjay);
    }
    return retval;
}

static int
ensure_registration_update_with_ctx(anjay_t *anjay,
                                    anjay_registration_update_ctx_t *ctx,
                                    anjay_server_info_t *server) {
    if (!_anjay_server_registration_connection_valid(server)) {
        anjay_log(INFO, "No valid existing connection to Registration "
                  "Interface for SSID = %u, re-registering", server->ssid);
        if (_anjay_server_setup_registration_connection(server)) {
            return 1;
        }
        return try_register(anjay, ctx);
    }

    if (_anjay_server_registration_expired(server)) {
        return try_register(anjay, ctx);
    }

    if (!_anjay_needs_registration_update(ctx)) {
        return 0;
    }

    int retval = _anjay_update_registration(ctx);
    switch (retval) {
    case 0:
        return 0;

    case ANJAY_REGISTRATION_UPDATE_REJECTED:
        anjay_log(DEBUG, "update rejected for SSID = %u; "
                         "needs re-registering", server->ssid);
        return try_register(anjay, ctx);

    case AVS_COAP_CTX_ERR_NETWORK:
        anjay_log(ERROR, "network communication error while updating "
                         "registration for SSID==%" PRIu16, server->ssid);
        // We cannot use _anjay_schedule_server_reconnect(),
        // because it would mean an endless loop without backoff
        // if the server is down. Instead, we disconnect the socket
        // and rely on scheduler's backoff. During the next call,
        // _anjay_server_refresh() will reconnect the socket.
        _anjay_connection_suspend((anjay_connection_ref_t) {
            .server = server,
            .conn_type = server->data_active.registration_info.conn_type
        });
        return retval;

    default:
        anjay_log(ERROR, "could not send registration update: %d", retval);
        return retval;
    }
}

int _anjay_server_ensure_valid_registration(anjay_t *anjay,
                                            anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    assert(server->ssid != ANJAY_SSID_BOOTSTRAP);

    anjay_registration_update_ctx_t ctx;
    if (_anjay_registration_update_ctx_init(anjay, &ctx, server)) {
        return -1;
    }

    int retval = ensure_registration_update_with_ctx(anjay, &ctx, server);
    if (!retval) {
        // Ignore errors, failure to flush notifications is not fatal.
        _anjay_observe_sched_flush(anjay, (anjay_connection_key_t) {
            .ssid = server->ssid,
            .type = server->data_active.registration_info.conn_type
        });
    }

    _anjay_registration_update_ctx_release(&ctx);

    if (!retval) {
        // Updates are retryable, we only need to reschedule after success
        retval = _anjay_server_reschedule_update_job(anjay, server);
    }
    return retval;
}

int _anjay_server_deregister(anjay_t *anjay,
                             anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = server->data_active.registration_info.conn_type
    };
    if (connection.conn_type == ANJAY_CONNECTION_UNSET
            || _anjay_bind_server_stream(anjay, connection)) {
        anjay_log(ERROR, "could not get stream for server %u, skipping",
                  server->ssid);
        return 0;
    }

    int result = _anjay_deregister(
            anjay, server->data_active.registration_info.endpoint_path);
    if (result) {
        anjay_log(ERROR, "could not send De-Register request: %d", result);
    }

    _anjay_release_server_stream_without_scheduling_queue(anjay);
    return result;
}

const anjay_registration_info_t *
_anjay_server_registration_info(anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    return &server->data_active.registration_info;
}

static avs_time_real_t get_registration_expire_time(int64_t lifetime_s) {
    return avs_time_real_add(avs_time_real_now(),
                             avs_time_duration_from_scalar(lifetime_s,
                                                           AVS_TIME_S));
}

void _anjay_server_update_registration_info(
        anjay_server_info_t *server,
        AVS_LIST(const anjay_string_t) *move_endpoint_path,
        anjay_update_parameters_t *move_params) {
    assert(_anjay_server_active(server));
    anjay_registration_info_t *info = &server->data_active.registration_info;

    if (move_endpoint_path && move_endpoint_path != &info->endpoint_path) {
        AVS_LIST_CLEAR(&info->endpoint_path);
        info->endpoint_path = *move_endpoint_path;
        *move_endpoint_path = NULL;
    }

    if (move_params && move_params != &info->last_update_params) {
        AVS_LIST(anjay_dm_cache_object_t) tmp = info->last_update_params.dm;
        info->last_update_params.dm = move_params->dm;
        move_params->dm = tmp;

        assert(move_params->lifetime_s >= 0);
        info->last_update_params.lifetime_s = move_params->lifetime_s;
        info->last_update_params.binding_mode = move_params->binding_mode;

        _anjay_update_parameters_cleanup(move_params);
    }

    info->expire_time =
            get_registration_expire_time(info->last_update_params.lifetime_s);
    info->needs_update = false;
}
