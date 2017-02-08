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

#include "../servers.h"
#include "../anjay.h"

#define ANJAY_SERVERS_INTERNALS

#include "connection_info.h"

VISIBILITY_SOURCE_BEGIN

static void disable_connection(anjay_server_connection_t *connection) {
    _anjay_connection_internal_set_move_socket(connection, NULL);
    connection->needs_socket_update = false;
}

static int enter_offline_job(anjay_t *anjay,
                             void *dummy) {
    (void) dummy;
    AVS_LIST(anjay_active_server_info_t) server;
    AVS_LIST_FOREACH(server, anjay->servers.active) {
        disable_connection(&server->udp_connection);
        _anjay_sched_del(anjay->sched, &server->sched_update_handle);
    }
    _anjay_sched_del(anjay->sched,
                     &anjay->servers.reload_sockets_sched_job_handle);
    anjay->offline = true;
    return 0;
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

static int exit_offline_job(anjay_t *anjay, void *dummy) {
    (void) dummy;
    int result = _anjay_schedule_reload_sockets(anjay);
    if (result) {
        return result;
    }
    anjay->offline = false;
    AVS_LIST(anjay_active_server_info_t) server;
    AVS_LIST_FOREACH(server, anjay->servers.active) {
        server->udp_connection.needs_socket_update = true;
    }
    return 0;
}

int anjay_exit_offline(anjay_t *anjay) {
    if (_anjay_sched_now(anjay->sched, NULL, exit_offline_job, NULL)) {
        anjay_log(ERROR, "could not schedule enter_offline_job");
        return -1;
    }
    return 0;
}
