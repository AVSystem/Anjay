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
#include "servers_internal.h"
#include "activate.h"
#include "register_internal.h"

VISIBILITY_SOURCE_BEGIN

static bool server_needs_reconnect(anjay_active_server_info_t *server) {
    anjay_connection_ref_t conn_ref = {
        .server = server
    };
    for (conn_ref.conn_type = (anjay_connection_type_t) 0;
            conn_ref.conn_type < ANJAY_CONNECTION_UNSET;
            conn_ref.conn_type =
                    (anjay_connection_type_t) (conn_ref.conn_type + 1)) {
        anjay_server_connection_t *connection =
                _anjay_get_server_connection(conn_ref);
        if (connection && connection->needs_reconnect) {
            return true;
        }
    }
    return false;
}

static int reload_active_server(anjay_t *anjay,
                                anjay_servers_t *servers,
                                AVS_LIST(anjay_active_server_info_t) server) {
    assert(AVS_LIST_SIZE(server) == 1);

    _anjay_servers_add_active(servers, server);

    if (server->needs_reload || server_needs_reconnect(server)) {
        server->needs_reload = false;
        if (_anjay_server_refresh(anjay, server, false)) {
            return -1;
        }
    }

    if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
        if (!_anjay_server_registration_connection_valid(server)
                || _anjay_server_registration_expired(server)) {
            server_registration_operation_t attempted_operation;
            int result = _anjay_server_update_or_reregister(
                    anjay, server, &attempted_operation);
            if (result && attempted_operation == SERVER_REGISTRATION_RETRY) {
                _anjay_server_deactivate(anjay, servers, server->ssid,
                                         AVS_TIME_DURATION_ZERO);
            }
        }

        // Flush notifications pending since connectivity loss or entering
        // offline mode.
        // Ignore errors, failure to flush notifications is not fatal.
        anjay_connection_key_t key = {
            .ssid = server->ssid,
            .type = server->registration_info.conn_type
        };
        _anjay_observe_sched_flush(anjay, key);

        if (!server->sched_update_handle) {
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
        _anjay_servers_add_inactive(new_servers,
                                    AVS_LIST_DETACH(inactive_server_ptr));
        return 0;
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
                                        ssid, AVS_TIME_DURATION_ZERO);
}

typedef struct {
    anjay_servers_t *old_servers;
    anjay_servers_t *new_servers;
    int retval;
} reload_servers_state_t;

static int
reload_server_by_security_iid(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              anjay_iid_t iid,
                              void *state_) {
    (void) obj;
    reload_servers_state_t *state = (reload_servers_state_t *) state_;

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

static int reload_servers_sched_job(anjay_t *anjay, void *unused) {
    (void)unused;
    anjay_log(TRACE, "reloading servers");

    anjay_servers_t reloaded_servers = _anjay_servers_create();
    reload_servers_state_t reload_state = {
        .old_servers = &anjay->servers,
        .new_servers = &reloaded_servers,
        .retval = 0
    };

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (obj && (_anjay_dm_foreach_instance(anjay, obj,
                                           reload_server_by_security_iid,
                                           &reload_state)
                    || reload_state.retval)) {
        // re-add old servers, don't discard them
        while (anjay->servers.active) {
            _anjay_servers_add_active(&reloaded_servers,
                                      AVS_LIST_DETACH(&anjay->servers.active));
        }
        _anjay_servers_cleanup(anjay);
        anjay->servers = reloaded_servers;
        anjay_log(ERROR, "reloading servers failed, re-scheduling job");
        _anjay_schedule_delayed_reload_servers(anjay);
    } else {
        if (obj) {
            anjay_log(INFO, "servers reloaded");
        } else {
            anjay_log(WARNING,
                      "Security object not present, no servers to create");
        }
        _anjay_servers_cleanup(anjay);
        anjay->servers = reloaded_servers;
    }

    anjay_log(TRACE, "servers reloaded; %lu active, %lu inactive",
              (unsigned long)AVS_LIST_SIZE(anjay->servers.active),
              (unsigned long)AVS_LIST_SIZE(anjay->servers.inactive));
    return 0;
}

static int schedule_reload_servers(anjay_t *anjay, bool delayed) {
    static const long RELOAD_DELAY_S = 5;
    _anjay_sched_del(anjay->sched, &anjay->reload_servers_sched_job_handle);
    if (_anjay_sched(anjay->sched, &anjay->reload_servers_sched_job_handle,
                     avs_time_duration_from_scalar(delayed ? RELOAD_DELAY_S : 0,
                                                   AVS_TIME_S),
                     reload_servers_sched_job, NULL)) {
        anjay_log(ERROR, "could not schedule reload_servers_job");
        return -1;
    }
    return 0;
}

int _anjay_schedule_reload_servers(anjay_t *anjay) {
    return schedule_reload_servers(anjay, false);
}

int _anjay_schedule_delayed_reload_servers(anjay_t *anjay) {
    return schedule_reload_servers(anjay, true);
}
