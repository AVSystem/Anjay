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

#include <avsystem/commons/errno.h>
#include <inttypes.h>

#include "../dm/query.h"
#include "../servers_utils.h"

#define ANJAY_SERVERS_INTERNALS

#include "activate.h"
#include "register_internal.h"
#include "reload.h"
#include "server_connections.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

void _anjay_server_on_server_communication_error(anjay_t *anjay,
                                                 anjay_server_info_t *server) {
    _anjay_server_clean_active_data(anjay, server);
    server->refresh_failed = true;

    if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        anjay_log(DEBUG, "Bootstrap Server could not be reached. "
                         "Disabling all communication.");
        // Abort any further bootstrap retries.
        _anjay_bootstrap_cleanup(anjay);
    } else if (_anjay_should_retry_bootstrap(anjay)) {
        if (_anjay_servers_find_active(anjay, ANJAY_SSID_BOOTSTRAP)) {
            _anjay_bootstrap_request_if_appropriate(anjay);
        } else {
            anjay_enable_server(anjay, ANJAY_SSID_BOOTSTRAP);
        }
    } else {
        anjay_log(DEBUG,
                  "Non-Bootstrap Server %" PRIu16 " could not be reached.",
                  server->ssid);
    }
    // make sure that the server will not be reactivated at next refresh
    server->reactivate_time = AVS_TIME_REAL_INVALID;
}

void _anjay_server_on_registration_timeout(anjay_t *anjay,
                                           anjay_server_info_t *server) {
    anjay_connection_ref_t ref = {
        .server = server,
        .conn_type = _anjay_server_primary_conn_type(server)
    };
    assert(server);
    assert(ref.conn_type != ANJAY_CONNECTION_UNSET);
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    if (connection->state == ANJAY_SERVER_CONNECTION_STABLE
            && connection->stateful
            && !_anjay_server_deactivate(anjay, server->ssid,
                                         AVS_TIME_DURATION_ZERO)) {
        server->refresh_failed = true;
    } else {
        _anjay_server_on_server_communication_error(anjay, server);
    }
}

void _anjay_server_on_refreshed(anjay_t *anjay,
                                anjay_server_info_t *server,
                                anjay_server_connection_state_t state) {
    assert(server);
    if (state == ANJAY_SERVER_CONNECTION_ERROR) {
        anjay_log(TRACE, "could not initialize sockets for SSID %u",
                  server->ssid);
        _anjay_server_on_server_communication_error(anjay, server);
    } else if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        if (_anjay_should_retry_bootstrap(anjay)) {
            server->refresh_failed =
                    !!_anjay_bootstrap_request_if_appropriate(anjay);
        } else {
            server->refresh_failed = false;
            _anjay_connection_mark_stable((anjay_connection_ref_t) {
                .server = server,
                .conn_type = _anjay_server_primary_conn_type(server)
            });
        }
        if (!server->refresh_failed) {
            server->reactivate_time = AVS_TIME_REAL_INVALID;
        }
        // _anjay_bootstrap_request_if_appropriate() may fail only due to
        // failure to schedule a job. Not much that we can do about it then.
    } else {
        switch (_anjay_server_ensure_valid_registration(anjay, server)) {
        case ANJAY_REGISTRATION_SUCCESS:
            server->reactivate_time = AVS_TIME_REAL_INVALID;
            server->refresh_failed = false;
            // Failure to handle Bootstrap state is not a failure of the
            // Register operation - hence, not checking return value.
            _anjay_bootstrap_notify_regular_connection_available(anjay);
            break;
        case ANJAY_REGISTRATION_TIMEOUT:
            _anjay_server_on_registration_timeout(anjay, server);
            break;
        case ANJAY_REGISTRATION_ERROR:
            _anjay_server_on_server_communication_error(anjay, server);
            break;
        }
    }
}

bool _anjay_can_retry_with_normal_server(anjay_t *anjay) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it) || it->ssid == ANJAY_SSID_BOOTSTRAP) {
            continue;
        }
        if (!it->refresh_failed) {
            // there is hope for a successful non-bootstrap connection
            return true;
        }
    }
    return false;
}

bool _anjay_should_retry_bootstrap(anjay_t *anjay) {
#ifdef WITH_BOOTSTRAP
    bool bootstrap_server_exists = false;
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
        } else if (_anjay_server_active(it)) {
            // Bootstrap Server is not the only active one
            return false;
        }
    }
    return bootstrap_server_exists
           && !_anjay_can_retry_with_normal_server(anjay);
#else  // WITH_BOOTSTRAP
    (void) anjay;
    return false;
#endif // WITH_BOOTSTRAP
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

int _anjay_server_sched_activate(anjay_t *anjay,
                                 anjay_server_info_t *server,
                                 avs_time_duration_t reactivate_delay) {
    // start the backoff procedure from the beginning
    assert(!_anjay_server_active(server));
    server->reactivate_time =
            avs_time_real_add(avs_time_real_now(), reactivate_delay);
    server->refresh_failed = false;
    return _anjay_schedule_refresh_server(anjay, server, reactivate_delay);
}

int _anjay_servers_sched_reactivate_all_given_up(anjay_t *anjay) {
    int result = 0;

    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it) || !it->refresh_failed) {
            continue;
        }
        int partial =
                _anjay_server_sched_activate(anjay, it, AVS_TIME_DURATION_ZERO);
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
        anjay_log(ERROR, "SSID %" PRIu16 " is not a known server", ssid);
        return -1;
    }

    if (_anjay_server_active(*server_ptr)
            && !_anjay_server_registration_expired(*server_ptr)) {
        // Return value intentionally ignored.
        // There isn't much we can do in case it fails and De-Register is
        // optional anyway. _anjay_serve_deregister logs the error cause.
        _anjay_server_deregister(anjay, *server_ptr);
    }
    _anjay_server_clean_active_data(anjay, *server_ptr);
    (*server_ptr)->registration_info.expire_time = AVS_TIME_REAL_INVALID;
    if (avs_time_duration_valid(reactivate_delay)
            && _anjay_server_sched_activate(anjay, *server_ptr,
                                            reactivate_delay)) {
        // not much we can do other than removing the server altogether
        anjay_log(ERROR, "could not reschedule server reactivation");
        AVS_LIST_DELETE(server_ptr);
        return -1;
    }
    return 0;
}

AVS_LIST(anjay_server_info_t)
_anjay_servers_create_inactive(anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) new_server =
            AVS_LIST_NEW_ELEMENT(anjay_server_info_t);
    if (!new_server) {
        anjay_log(ERROR, "out of memory");
        return NULL;
    }

    new_server->ssid = ssid;
    new_server->reactivate_time = AVS_TIME_REAL_INVALID;
    return new_server;
}
