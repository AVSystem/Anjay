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

#include <errno.h>
#include <inttypes.h>

#include <anjay_modules/time_defs.h>

#include <avsystem/commons/memory.h>

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_core.h"
#include "../dm/query.h"
#include "../interface/register.h"
#include "../servers.h"
#include "../servers_utils.h"

#include "activate.h"
#include "register_internal.h"
#include "server_connections.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

void _anjay_server_clean_active_data(const anjay_t *anjay,
                                     anjay_server_info_t *server) {
    _anjay_sched_del(anjay->sched, &server->next_action_handle);
    _anjay_connections_close(anjay, &server->connections);
}

void _anjay_server_cleanup(const anjay_t *anjay, anjay_server_info_t *server) {
    anjay_log(TRACE, "clear_server SSID %u", server->ssid);

    _anjay_server_clean_active_data(anjay, server);
    _anjay_registration_info_cleanup(&server->registration_info);
}

anjay_servers_t *_anjay_servers_create(void) {
    return (anjay_servers_t *) avs_calloc(1, sizeof(anjay_servers_t));
}

void _anjay_servers_internal_deregister(anjay_t *anjay,
                                        anjay_servers_t *servers) {
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, servers->servers) {
        if (_anjay_server_active(server) && server->ssid != ANJAY_SSID_BOOTSTRAP
                && !_anjay_server_registration_expired(server)) {
            _anjay_server_deregister(anjay, server);
        }
    }
}

void _anjay_servers_internal_cleanup(anjay_t *anjay, anjay_servers_t *servers) {
    anjay_log(TRACE, "cleaning up %lu servers",
              (unsigned long) AVS_LIST_SIZE(servers->servers));

    AVS_LIST_CLEAR(&servers->servers) {
        _anjay_server_cleanup(anjay, servers->servers);
    }
    AVS_LIST_CLEAR(&servers->public_sockets);
}

void _anjay_servers_deregister(anjay_t *anjay) {
    if (anjay->servers) {
        _anjay_servers_internal_deregister(anjay, anjay->servers);
    }
}

void _anjay_servers_cleanup(anjay_t *anjay) {
    if (anjay->servers) {
        _anjay_servers_internal_cleanup(anjay, anjay->servers);
        avs_free(anjay->servers);
        anjay->servers = NULL;
    }
}

void _anjay_servers_cleanup_inactive(anjay_t *anjay) {
    AVS_LIST(anjay_server_info_t) *server_ptr;
    AVS_LIST(anjay_server_info_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(server_ptr, helper,
                                   &anjay->servers->servers) {
        if (!_anjay_server_active(*server_ptr)) {
            _anjay_server_cleanup(anjay, *server_ptr);
            AVS_LIST_DELETE(server_ptr);
        }
    }
}

avs_net_abstract_socket_t *
_anjay_connection_get_online_socket(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    if (!_anjay_connection_is_online(connection)) {
        return NULL;
    }
    return _anjay_connection_internal_get_socket(connection);
}

static int add_socket_onto_list(AVS_LIST(anjay_socket_entry_t) *tail_ptr,
                                avs_net_abstract_socket_t *socket,
                                anjay_socket_transport_t transport,
                                anjay_ssid_t ssid,
                                bool queue_mode) {
    assert(!*tail_ptr);
    AVS_LIST_INSERT_NEW(anjay_socket_entry_t, tail_ptr);
    if (!*tail_ptr) {
        anjay_log(ERROR, "Out of memory while building socket list");
        return -1;
    }
    (*tail_ptr)->socket = socket;
    (*tail_ptr)->transport = transport;
    (*tail_ptr)->ssid = ssid;
    (*tail_ptr)->queue_mode = queue_mode;
    return 0;
}

/**
 * Repopulates the public_sockets list, adding to it all online UDP LwM2M
 * sockets, the single SMS router socket (if applicable) and all active
 * download sockets (if applicable).
 */
AVS_LIST(const anjay_socket_entry_t) anjay_get_socket_entries(anjay_t *anjay) {
    AVS_LIST_CLEAR(&anjay->servers->public_sockets);
    AVS_LIST(anjay_socket_entry_t) *tail_ptr = &anjay->servers->public_sockets;

    // Note that there is at most one SMS socket (as the modem connection is
    // common to all servers) so "sms_active" and "sms_queue_mode" are common
    // for all of them.
    bool sms_active = false;
    bool sms_queue_mode = true;
    anjay_connection_ref_t ref;
    AVS_LIST_FOREACH(ref.server, anjay->servers->servers) {
        if (!_anjay_server_active(ref.server)) {
            continue;
        }

        ref.conn_type = ANJAY_CONNECTION_UDP;
        anjay_server_connection_t *udp_connection =
                _anjay_get_server_connection(ref);
        assert(udp_connection);
        avs_net_abstract_socket_t *udp_socket =
                _anjay_connection_internal_get_socket(udp_connection);
        if (udp_socket && _anjay_connection_is_online(udp_connection)
                && !add_socket_onto_list(
                           tail_ptr, udp_socket, ANJAY_SOCKET_TRANSPORT_UDP,
                           ref.server->ssid,
                           udp_connection->mode == ANJAY_CONNECTION_QUEUE)) {
            AVS_LIST_ADVANCE_PTR(&tail_ptr);
        }

#ifdef WITH_SMS
        ref.conn_type = ANJAY_CONNECTION_SMS;
        anjay_server_connection_t *sms_connection =
                _anjay_get_server_connection(ref);
        assert(sms_connection);
        if (_anjay_connection_is_online(sms_connection)) {
            sms_active = true;
            if (sms_connection->mode == ANJAY_CONNECTION_ONLINE) {
                sms_queue_mode = false;
            }
        }
#endif // WITH_SMS
    }

    if (sms_active) {
        assert(_anjay_sms_router(anjay));
        add_socket_onto_list(tail_ptr, _anjay_sms_poll_socket(anjay),
                             ANJAY_SOCKET_TRANSPORT_SMS, ANJAY_SSID_ANY,
                             sms_queue_mode);
    }

#ifdef WITH_DOWNLOADER
    _anjay_downloader_get_sockets(&anjay->downloader, tail_ptr);
#endif // WITH_DOWNLOADER
    return anjay->servers->public_sockets;
}

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_insert_ptr(anjay_servers_t *servers, anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) *it;
    AVS_LIST_FOREACH_PTR(it, &servers->servers) {
        if ((*it)->ssid >= ssid) {
            return it;
        }
    }
    return it;
}

AVS_LIST(anjay_server_info_t) *_anjay_servers_find_ptr(anjay_servers_t *servers,
                                                       anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) *ptr =
            _anjay_servers_find_insert_ptr(servers, ssid);
    if (*ptr && (*ptr)->ssid == ssid) {
        return ptr;
    }

    anjay_log(TRACE, "no server with SSID %u", ssid);
    return NULL;
}

static void disable_server_job(anjay_t *anjay, const void *ssid_ptr) {
    anjay_ssid_t ssid = *(const anjay_ssid_t *) ssid_ptr;

    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        anjay_log(DEBUG,
                  "no Server Object Instance with SSID = %u, disabling "
                  "skipped",
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
    if (_anjay_sched_now(anjay->sched, NULL, disable_server_job, &ssid,
                         sizeof(ssid))) {
        anjay_log(ERROR, "could not schedule disable_server_job");
        return -1;
    }
    return 0;
}

typedef struct {
    anjay_ssid_t ssid;
    avs_time_duration_t timeout;
} disable_server_data_t;

static void disable_server_with_timeout_job(anjay_t *anjay,
                                            const void *data_ptr_) {
    const disable_server_data_t *data =
            (const disable_server_data_t *) data_ptr_;
    if (_anjay_server_deactivate(anjay, data->ssid, data->timeout)) {
        anjay_log(ERROR, "unable to deactivate server: %" PRIu16, data->ssid);
    } else {
        if (avs_time_duration_valid(data->timeout)) {
            anjay_log(INFO,
                      "server %" PRIu16 " disabled for %" PRId64 ".%09" PRId32
                      " seconds",
                      data->ssid, data->timeout.seconds,
                      data->timeout.nanoseconds);
        } else {
            anjay_log(INFO, "server %" PRIu16 " disabled", data->ssid);
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
 *   Server-Initiated Bootstrap is disabled
 * - serv_execute(), as a reference implementation of the Disable resource
 * - _anjay_schedule_socket_update(), to force reconnection of all sockets
 */
int anjay_disable_server_with_timeout(anjay_t *anjay,
                                      anjay_ssid_t ssid,
                                      avs_time_duration_t timeout) {
    if (ssid == ANJAY_SSID_ANY) {
        anjay_log(WARNING, "invalid SSID: %u", ssid);
        return -1;
    }

    disable_server_data_t data = {
        .ssid = ssid,
        .timeout = timeout
    };

    if (_anjay_sched_now(anjay->sched, NULL, disable_server_with_timeout_job,
                         &data, sizeof(data))) {
        anjay_log(ERROR, "could not schedule disable_server_with_timeout_job");
        return -1;
    }

    return 0;
}

/**
 * Schedules server activation immediately, after some sanity checks.
 *
 * The activation request is rejected if someone tries to enable the Bootstrap
 * Server, Client-Initiated Bootstrap is not supposed to be performed, and
 * Server-Initiated Bootstrap is administratively disabled.
 */
int anjay_enable_server(anjay_t *anjay, anjay_ssid_t ssid) {
    if (ssid == ANJAY_SSID_ANY) {
        anjay_log(WARNING, "invalid SSID: %u", ssid);
        return -1;
    }

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(anjay->servers, ssid);

    if (!server_ptr || !*server_ptr || _anjay_server_active(*server_ptr)) {
        anjay_log(TRACE, "not an inactive server: SSID = %u", ssid);
        return -1;
    }

    if (ssid == ANJAY_SSID_BOOTSTRAP
            && !_anjay_bootstrap_server_initiated_allowed(anjay)
            && !_anjay_should_retry_bootstrap(anjay)) {
        anjay_log(TRACE, "Server-Initiated Bootstrap is disabled and "
                         "Client-Initiated Bootstrap is currently not allowed, "
                         "not enabling Bootstrap Server");
        return -1;
    }

    return _anjay_server_sched_activate(anjay, *server_ptr,
                                        AVS_TIME_DURATION_ZERO);
}

bool _anjay_server_active(anjay_server_info_t *server) {
    anjay_connection_ref_t ref = {
        .server = server
    };
    ANJAY_CONNECTION_TYPE_FOREACH(ref.conn_type) {
        if (_anjay_connection_internal_get_socket(
                    _anjay_get_server_connection(ref))) {
            return true;
        }
    }

    return false;
}

anjay_ssid_t _anjay_server_ssid(anjay_server_info_t *server) {
    return server->ssid;
}

anjay_connection_type_t
_anjay_server_primary_conn_type(anjay_server_info_t *server) {
    return _anjay_connections_get_primary(&server->connections);
}

int _anjay_servers_foreach_ssid(anjay_t *anjay,
                                anjay_servers_foreach_ssid_handler_t *handler,
                                void *data) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        int result = handler(anjay, it->ssid, data);
        if (result == ANJAY_FOREACH_BREAK) {
            anjay_log(DEBUG, "servers_foreach_ssid: break on %u", it->ssid);
            return 0;
        } else if (result) {
            anjay_log(ERROR, "servers_foreach_ssid handler failed for %u (%d)",
                      it->ssid, result);
            return result;
        }
    }

    return 0;
}

int _anjay_servers_foreach_active(anjay_t *anjay,
                                  anjay_servers_foreach_handler_t *handler,
                                  void *data) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (!_anjay_server_active(it)) {
            continue;
        }
        int result = handler(anjay, it, data);
        if (result == ANJAY_FOREACH_BREAK) {
            anjay_log(DEBUG, "servers_foreach_ssid: break on %u", it->ssid);
            return 0;
        } else if (result) {
            anjay_log(ERROR, "servers_foreach_ssid handler failed for %u (%d)",
                      it->ssid, result);
            return result;
        }
    }

    return 0;
}
