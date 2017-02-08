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

#include "../dm/query.h"

#define ANJAY_SERVERS_INTERNALS

#include "connection_info.h"
#include "servers.h"
#include "activate.h"
#include "register.h"

VISIBILITY_SOURCE_BEGIN

static int reload_inactive_server(anjay_t *anjay,
                                  anjay_servers_t *servers,
                                  AVS_LIST(anjay_inactive_server_info_t) server) {
    assert(AVS_LIST_SIZE(server) == 1);

    _anjay_servers_add_inactive(servers, server);

    if (server->needs_activation
            && _anjay_server_sched_activate(anjay, servers, server->ssid,
                                            ANJAY_TIME_ZERO)) {
        return -1;
    }

    return 0;
}

static int reload_active_server(anjay_t *anjay,
                                anjay_servers_t *servers,
                                AVS_LIST(anjay_active_server_info_t) server) {
    assert(AVS_LIST_SIZE(server) == 1);

    _anjay_servers_add_active(servers, server);

    if (server->udp_connection.needs_socket_update) {
        if (_anjay_server_refresh(anjay, server, false)) {
            return -1;
        }

        if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
            _anjay_server_update_or_reregister(anjay, server);
            return _anjay_server_reschedule_update_job(anjay, server);
        }
    }

    return 0;
}

static int reload_server_by_ssid(anjay_t *anjay,
                                 anjay_servers_t *old_servers,
                                 anjay_servers_t *new_servers,
                                 anjay_ssid_t ssid) {
    anjay_log(TRACE, "reloading server SSID %u", ssid);

    AVS_LIST(anjay_inactive_server_info_t) *inactive_server_ptr =
            _anjay_servers_find_inactive_ptr(old_servers, ssid);
    if (inactive_server_ptr) {
        anjay_log(TRACE, "reloading inactive server SSID %u", ssid);
        return reload_inactive_server(anjay, new_servers,
                                      AVS_LIST_DETACH(inactive_server_ptr));
    }

    AVS_LIST(anjay_active_server_info_t) *active_server_ptr =
            _anjay_servers_find_active_ptr(old_servers, ssid);
    if (active_server_ptr) {
        anjay_log(TRACE, "reloading active server SSID %u", ssid);
        return reload_active_server(anjay, new_servers,
                                    AVS_LIST_DETACH(active_server_ptr));
    }

    anjay_log(TRACE, "creating server SSID %u", ssid);
    AVS_LIST(anjay_inactive_server_info_t) new_server =
            _anjay_servers_create_inactive(ssid);
    if (!new_server) {
        return -1;
    }

    _anjay_servers_add_inactive(new_servers, new_server);
    return _anjay_server_sched_activate(anjay, new_servers,
                                        ssid, ANJAY_TIME_ZERO);
}

typedef struct {
    anjay_servers_t *old_servers;
    anjay_servers_t *new_servers;
    int retval;
} reload_server_sockets_state_t;

static int
reload_server_socket_by_security_iid(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     anjay_iid_t iid,
                                     void *state_) {
    (void) obj;
    reload_server_sockets_state_t *state =
            (reload_server_sockets_state_t *) state_;

    anjay_ssid_t ssid;
    if (_anjay_ssid_from_security_iid(anjay, iid, &ssid)) {
        state->retval = -1;
        return 0;
    }

    if (reload_server_by_ssid(anjay, state->old_servers,
                              state->new_servers, ssid)) {
        anjay_log(TRACE, "could not reload server SSID %u", ssid);
        state->retval = -1;
    }

    return 0;
}

static int reload_sockets_sched_job(anjay_t *anjay, void *unused);

static void reschedule_reload_sockets_job(anjay_t *anjay,
                                          const long reload_delay_s) {
    if (_anjay_sched(anjay->sched,
                     &anjay->servers.reload_sockets_sched_job_handle,
                     (struct timespec){.tv_sec = reload_delay_s },
                     reload_sockets_sched_job, NULL)) {
        anjay_log(ERROR, "could not re-schedule reload_sockets_job");
    }
}

static int reload_sockets_sched_job(anjay_t *anjay, void *unused) {
    (void)unused;
    anjay_log(TRACE, "reloading sockets");

    static const long RELOAD_DELAY_S = 5;
    anjay_servers_t reloaded_servers = _anjay_servers_create();
    reload_server_sockets_state_t reload_state = {
        .old_servers = &anjay->servers,
        .new_servers = &reloaded_servers,
        .retval = 0
    };

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (_anjay_dm_foreach_instance(anjay, obj,
                                   reload_server_socket_by_security_iid,
                                   &reload_state)
            || reload_state.retval) {
        // re-add old servers, don't discard them
        while (anjay->servers.active) {
            _anjay_servers_add_active(&reloaded_servers,
                                      AVS_LIST_DETACH(&anjay->servers.active));
        }
        _anjay_servers_cleanup(anjay, &anjay->servers);
        anjay->servers = reloaded_servers;
        anjay_log(ERROR, "reloading sockets failed, re-scheduling job with "
                         "%lds delay",
                  RELOAD_DELAY_S);
        reschedule_reload_sockets_job(anjay, RELOAD_DELAY_S);
    } else {
        anjay_log(INFO, "sockets reloaded");
        _anjay_servers_cleanup(anjay, &anjay->servers);
        anjay->servers = reloaded_servers;
    }

    anjay_log(TRACE, "sockets reloaded; %lu active, %lu inactive",
              (unsigned long)AVS_LIST_SIZE(anjay->servers.active),
              (unsigned long)AVS_LIST_SIZE(anjay->servers.inactive));
    return 0;
}

int _anjay_schedule_reload_sockets(anjay_t *anjay) {
    _anjay_sched_del(anjay->sched, &anjay->servers.reload_sockets_sched_job_handle);
    if (_anjay_sched_now(anjay->sched,
                         &anjay->servers.reload_sockets_sched_job_handle,
                         reload_sockets_sched_job, NULL)) {
        anjay_log(ERROR, "could not schedule reload_sockets_job");
        return -1;
    }
    return 0;
}
