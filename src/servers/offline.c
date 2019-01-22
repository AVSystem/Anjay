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

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_core.h"
#include "../servers.h"

#include "reload.h"
#include "server_connections.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static void enter_offline_job(anjay_t *anjay, const void *dummy) {
    (void) dummy;
    avs_time_real_t now = avs_time_real_now();
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, anjay->servers->servers) {
        _anjay_sched_del(anjay->sched, &server->next_action_handle);
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
            ANJAY_CONNECTION_TYPE_FOREACH(ref.conn_type) {
                _anjay_connection_internal_clean_socket(
                        anjay, _anjay_get_server_connection(ref));
            }
            server->reactivate_time = now;
        }
    }
    _anjay_sched_del(anjay->sched, &anjay->reload_servers_sched_job_handle);
    anjay->offline = true;
}

/**
 * Just a getter for the offline flag.
 */
bool anjay_is_offline(anjay_t *anjay) {
    return anjay->offline;
}

/**
 * Enters the offline mode, which is basically deactivating all the servers and
 * setting the offline flag to true. The servers can't be deactivated using
 * _anjay_server_deactivate(), because that would deregister the server and
 * explicitly invalidate its registration information - we want to preserve the
 * registration state here, so that we can try to resume DTLS sessions after
 * getting out of the offline mode.
 *
 * This is done through a scheduled job, because otherwise if someone called
 * anjay_enter_offline() from within a data model handler, it would close the
 * connection on which we're supposed to send the response, and probably
 * everything would burn when the code attempts to actually send it.
 */
int anjay_enter_offline(anjay_t *anjay) {
    if (_anjay_sched_now(anjay->sched, NULL, enter_offline_job, NULL, 0)) {
        anjay_log(ERROR, "could not schedule enter_offline_job");
        return -1;
    }
    return 0;
}

/**
 * Schedules the exit from offline mode - just clearing the offline flag and
 * scheduling reload of the servers - thanks to the reactivate_time logic, they
 * will be properly reactivated during reload.
 */
int anjay_exit_offline(anjay_t *anjay) {
    int result = _anjay_schedule_reload_servers(anjay);
    if (!result) {
        anjay->offline = false;
    }
    return result;
}
