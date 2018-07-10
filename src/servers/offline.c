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

#include <anjay_config.h>

#define ANJAY_SERVERS_INTERNALS

#include "../servers.h"
#include "../anjay_core.h"

#include "connection_info.h"
#include "reload.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

// TODO: Offline and SMS? How to interpret?
static void disable_connection(anjay_server_connection_t *connection) {
    _anjay_connection_internal_clean_socket(connection);
    connection->needs_reconnect = false;
}

static void enter_offline_job(anjay_t *anjay, void *dummy) {
    (void) dummy;
    avs_time_real_t now = avs_time_real_now();
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, anjay->servers->servers) {
        _anjay_sched_del(anjay->sched,
                         &server->sched_update_or_reactivate_handle);
        if (_anjay_server_active(server)) {
            // If the server is active, we clean up all its sockets, essentially
            // deactivating it. We don't schedule reactivation, as that would
            // fill the sched_update_or_reactivate_handle field we have just
            // cleared - but rather we store the reactivation time of "now".
            // Note that after exiting offline mode, this will schedule
            // reactivation with negative delay, but it will effectively cause
            // the reactivation to be scheduled for immediate execution.
            anjay_connection_ref_t ref = {
                .server = server
            };
            for (ref.conn_type = ANJAY_CONNECTION_FIRST_VALID_;
                    ref.conn_type < ANJAY_CONNECTION_LIMIT_;
                    ref.conn_type =
                            (anjay_connection_type_t) (ref.conn_type + 1)) {
                anjay_server_connection_t *connection =
                        _anjay_get_server_connection(ref);
                if (connection) {
                    disable_connection(connection);
                }
            }
            server->data_inactive.reactivate_time = now;
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

int anjay_exit_offline(anjay_t *anjay) {
    return _anjay_schedule_reconnect_servers(anjay);
}
