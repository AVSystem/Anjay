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

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_core.h"
#include "../anjay_servers.h"
#include "../anjay_servers_inactive.h"

#include "anjay_reload.h"
#include "anjay_server_connections.h"
#include "anjay_servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static void enter_offline_job(avs_sched_t *sched, const void *dummy) {
    (void) dummy;
    anjay_t *anjay = _anjay_get_from_sched(sched);
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, anjay->servers->servers) {
        avs_sched_del(&server->next_action_handle);
        if (_anjay_server_active(server)) {
            anjay_connection_ref_t ref = {
                .server = server
            };
            ANJAY_CONNECTION_TYPE_FOREACH(ref.conn_type) {
                anjay_server_connection_t *conn =
                        _anjay_get_server_connection(ref);
                avs_net_socket_t *socket =
                        _anjay_connection_internal_get_socket(conn);
                if (ref.conn_type == ANJAY_CONNECTION_PRIMARY && conn->coap_ctx
                        && avs_coap_exchange_id_valid(
                                   server->registration_exchange_state
                                           .exchange_id)) {
                    avs_coap_exchange_cancel(
                            conn->coap_ctx,
                            server->registration_exchange_state.exchange_id);
                }
                _anjay_observe_interrupt(ref);
                if (socket) {
                    avs_net_socket_shutdown(socket);
                    avs_net_socket_close(socket);
                }
            }
        }
    }
    avs_sched_del(&anjay->reload_servers_sched_job_handle);
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
    avs_sched_del(&anjay->enter_offline_job_handle);
    if (AVS_SCHED_NOW(anjay->sched, &anjay->enter_offline_job_handle,
                      enter_offline_job, NULL, 0)) {
        anjay_log(ERROR, _("could not schedule enter_offline_job"));
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
        avs_sched_del(&anjay->enter_offline_job_handle);
        anjay->offline = false;
    }
    return result;
}
