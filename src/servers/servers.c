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

#include <errno.h>
#include <inttypes.h>

#include <anjay_modules/time.h>

#include "../dm/query.h"
#include "../anjay.h"
#include "../servers.h"
#include "../interface/register.h"

#define ANJAY_SERVERS_INTERNALS

#include "activate.h"
#include "connection_info.h"
#include "register.h"
#include "servers.h"

VISIBILITY_SOURCE_BEGIN

static void connection_cleanup(anjay_t *anjay,
                               anjay_server_connection_t *connection) {
    _anjay_connection_internal_set_move_socket(connection, NULL);
    _anjay_sched_del(anjay->sched,
                     &connection->queue_mode_suspend_socket_clb_handle);
}

void _anjay_server_cleanup(anjay_t *anjay, anjay_active_server_info_t *server) {
    anjay_log(TRACE, "clear_server SSID %u", server->ssid);

    _anjay_sched_del(anjay->sched, &server->sched_update_handle);
    _anjay_registration_info_cleanup(&server->registration_info);
    connection_cleanup(anjay, &server->udp_connection);
}

static void active_servers_delete_and_deregister(
        anjay_t *anjay,
        AVS_LIST(anjay_active_server_info_t) *servers_ptr) {
    anjay_log(TRACE, "servers_delete_and_deregister");

    AVS_LIST_CLEAR(servers_ptr) {
        if ((*servers_ptr)->ssid != ANJAY_SSID_BOOTSTRAP) {
            _anjay_server_deregister(anjay, *servers_ptr);
        }
        _anjay_server_cleanup(anjay, *servers_ptr);
    }
}

void _anjay_servers_cleanup(anjay_t *anjay,
                            anjay_servers_t *servers) {
    anjay_log(TRACE, "cleanup servers: %lu active, %lu inactive",
              (unsigned long)AVS_LIST_SIZE(servers->active),
              (unsigned long)AVS_LIST_SIZE(servers->inactive));

    _anjay_sched_del(anjay->sched, &servers->reload_sockets_sched_job_handle);
    active_servers_delete_and_deregister(anjay, &servers->active);
    AVS_LIST_CLEAR(&servers->inactive) {
        _anjay_sched_del(anjay->sched,
                         &servers->inactive->sched_reactivate_handle);
    }
    AVS_LIST_CLEAR(&servers->nonqueue_sockets);
}

static bool is_connection_online(anjay_t *anjay,
                                 anjay_ssid_t ssid,
                                 const anjay_server_connection_t *connection) {
    (void) anjay; (void) ssid;
    return _anjay_connection_internal_get_socket(connection) != NULL
#ifdef WITH_BOOTSTRAP
            && (!anjay->bootstrap.in_progress || ssid == ANJAY_SSID_BOOTSTRAP)
#endif
            && (!connection->queue_mode
                    || connection->queue_mode_suspend_socket_clb_handle);
    // see comment on field declaration for logic summary
}

static void discard_old_packets(avs_net_abstract_socket_t *socket) {
    avs_net_socket_opt_value_t old_timeout;
    if (avs_net_socket_get_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                               &old_timeout)
            || avs_net_socket_set_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                      (avs_net_socket_opt_value_t) {
                                          .recv_timeout = 0
                                      })) {
        anjay_log(ERROR, "could not set socket recv timeout");
        return;
    }
    size_t ignore;
    int receive_result;
    do {
        receive_result = avs_net_socket_receive(socket, &ignore, &ignore, 0);
    } while (receive_result == 0 || avs_net_socket_errno(socket) == EMSGSIZE);
    if (avs_net_socket_set_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                               old_timeout)) {
        anjay_log(ERROR, "could not restore socket recv timeout");
    }
}

avs_net_abstract_socket_t *
_anjay_connection_get_prepared_socket(anjay_server_connection_t *connection) {
    avs_net_abstract_socket_t *socket = NULL;
    if (connection) {
        socket = _anjay_connection_internal_get_socket(connection);
    }
    if (!socket) {
        return NULL;
    }
    if (connection->needs_discard_old_packets) {
        discard_old_packets(socket);
        connection->needs_discard_old_packets = false;
    }
    return socket;
}

AVS_LIST(avs_net_abstract_socket_t *const) anjay_get_sockets(anjay_t *anjay) {
    AVS_LIST_CLEAR(&anjay->servers.nonqueue_sockets);
    AVS_LIST(avs_net_abstract_socket_t *const) *tail_ptr =
            &anjay->servers.nonqueue_sockets;

    anjay_active_server_info_t *server;
    AVS_LIST_FOREACH(server, anjay->servers.active) {
        if (is_connection_online(anjay, server->ssid,
                                 &server->udp_connection)) {
            AVS_LIST_INSERT_NEW(avs_net_abstract_socket_t *const, tail_ptr);
            if (!*tail_ptr) {
                anjay_log(ERROR, "Out of memory while building socket list");
            } else {
                *(avs_net_abstract_socket_t **) (intptr_t) *tail_ptr
                        = _anjay_connection_get_prepared_socket(
                                &server->udp_connection);
                tail_ptr = AVS_LIST_NEXT_PTR(tail_ptr);
            }
        } else {
            server->udp_connection.needs_discard_old_packets = true;
        }
    }
    return anjay->servers.nonqueue_sockets;
}

anjay_active_server_info_t *
_anjay_servers_find_by_udp_socket(anjay_servers_t *servers,
                                  avs_net_abstract_socket_t *socket) {
    AVS_LIST(anjay_active_server_info_t) it;
    AVS_LIST_FOREACH(it, servers->active) {
        if (_anjay_connection_internal_get_socket(&it->udp_connection)
                == socket) {
            return it;
        }
    }

    return NULL;
}

int _anjay_schedule_socket_update(anjay_t *anjay,
                                  anjay_iid_t security_iid) {
    anjay_ssid_t ssid;
    anjay_active_server_info_t *server;
    if (!_anjay_ssid_from_security_iid(anjay, security_iid, &ssid)
            && (server = _anjay_servers_find_active(&anjay->servers, ssid))) {
        server->udp_connection.needs_socket_update = true;
    }
    return _anjay_schedule_reload_sockets(anjay);
}

#ifdef WITH_BOOTSTRAP
bool _anjay_servers_is_connected_to_non_bootstrap(anjay_servers_t *servers) {
    AVS_LIST(anjay_active_server_info_t) server;
    AVS_LIST_FOREACH(server, servers->active) {
        if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
            return true;
        }
    }
    return false;
}
#endif

AVS_LIST(anjay_active_server_info_t) *
_anjay_servers_find_active_insert_ptr(anjay_servers_t *servers,
                                      anjay_ssid_t ssid) {
    AVS_LIST(anjay_active_server_info_t) *it;
    AVS_LIST_FOREACH_PTR(it, &servers->active) {
        if ((*it)->ssid >= ssid) {
            return it;
        }
    }
    return it;
}

AVS_LIST(anjay_active_server_info_t) *
_anjay_servers_find_active_ptr(anjay_servers_t *servers,
                               anjay_ssid_t ssid) {
    AVS_LIST(anjay_active_server_info_t) *ptr =
            _anjay_servers_find_active_insert_ptr(servers, ssid);
    if (*ptr && (*ptr)->ssid == ssid) {
        return ptr;
    }

    anjay_log(TRACE, "no active server with SSID %u", ssid);
    return NULL;
}

anjay_active_server_info_t *_anjay_servers_find_active(anjay_servers_t *servers,
                                                       anjay_ssid_t ssid) {
    AVS_LIST(anjay_active_server_info_t) *ptr =
            _anjay_servers_find_active_ptr(servers, ssid);
    return ptr ? *ptr : NULL;
}

AVS_LIST(anjay_inactive_server_info_t) *
_anjay_servers_find_inactive_insert_ptr(anjay_servers_t *servers,
                                        anjay_ssid_t ssid) {
    AVS_LIST(anjay_inactive_server_info_t) *it;
    AVS_LIST_FOREACH_PTR(it, &servers->inactive) {
        if ((*it)->ssid >= ssid) {
            break;
        }
    }
    return it;
}

AVS_LIST(anjay_inactive_server_info_t) *
_anjay_servers_find_inactive_ptr(anjay_servers_t *servers,
                                 anjay_ssid_t ssid) {
    AVS_LIST(anjay_inactive_server_info_t) *ptr =
            _anjay_servers_find_inactive_insert_ptr(servers, ssid);
    if (*ptr && (*ptr)->ssid == ssid) {
        return ptr;
    }

    anjay_log(TRACE, "no inactive server with SSID %u", ssid);
    return NULL;
}

static int disable_server_job(anjay_t *anjay,
                              void *ssid_) {
    anjay_ssid_t ssid = (anjay_ssid_t)(intptr_t)ssid_;

    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        anjay_log(DEBUG, "no Server Object Instance with SSID = %u, disabling "
                  "skipped", ssid);
        return 0;
    }

    struct timespec reactivate_delay =
            _anjay_disable_timeout_from_server_iid(anjay, server_iid);

    _anjay_server_deactivate(anjay, &anjay->servers, ssid, reactivate_delay);
    return 0;
}

int anjay_disable_server(anjay_t *anjay,
                         anjay_ssid_t ssid) {
    if (_anjay_sched_now(anjay->sched, NULL, disable_server_job,
                         (void *) (uintptr_t) ssid)) {
        anjay_log(ERROR, "could not schedule disable_server_job");
        return -1;
    }
    return 0;
}
