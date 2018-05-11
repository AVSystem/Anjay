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

#define ANJAY_SERVERS_INTERNALS

#include "../servers.h"
#include "../anjay_core.h"

#include "connection_info.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

// TODO: Offline and SMS? How to interpret?
static void disable_connection(anjay_server_connection_t *connection) {
    avs_net_abstract_socket_t *socket =
            _anjay_connection_internal_get_socket(connection);
    if (socket) {
        avs_net_socket_close(socket);
    }
    connection->needs_reconnect = false;
}

static void enter_offline_job(anjay_t *anjay, void *dummy) {
    (void) dummy;
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, anjay->servers->servers) {
        if (_anjay_server_active(server)) {
            disable_connection(&server->data_active.udp_connection);
            _anjay_sched_del(anjay->sched,
                             &server->sched_update_or_reactivate_handle);
        }
    }
    _anjay_sched_del(anjay->sched, &anjay->reload_servers_sched_job_handle);
    anjay->offline = true;
}

bool anjay_is_offline(anjay_t *anjay) {
    return anjay->offline;
}

int anjay_enter_offline(anjay_t *anjay) {
    if (_anjay_sched_now(anjay->sched, NULL, enter_offline_job, NULL)) {
        anjay_log(ERROR, "could not schedule enter_offline_job");
        return -1;
    }
    return 0;
}

static void exit_offline_job(anjay_t *anjay, void *dummy) {
    (void) dummy;
    if (!_anjay_schedule_reload_servers(anjay)) {
        anjay->offline = false;
        AVS_LIST(anjay_server_info_t) server;
        AVS_LIST_FOREACH(server, anjay->servers->servers) {
            if (_anjay_server_active(server)) {
                server->data_active.udp_connection.needs_reconnect = true;
            }
        }
    }
}

int anjay_exit_offline(anjay_t *anjay) {
    if (_anjay_sched_now(anjay->sched, NULL, exit_offline_job, NULL)) {
        anjay_log(ERROR, "could not schedule enter_offline_job");
        return -1;
    }
    return 0;
}
