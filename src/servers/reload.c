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

#include "../dm/query.h"

#define ANJAY_SERVERS_INTERNALS

#include "connection_info.h"
#include "servers_internal.h"
#include "activate.h"
#include "register_internal.h"
#include "reload.h"

VISIBILITY_SOURCE_BEGIN

static bool server_needs_reconnect(anjay_server_info_t *server) {
    anjay_connection_ref_t conn_ref = {
        .server = server
    };
    for (conn_ref.conn_type = ANJAY_CONNECTION_FIRST_VALID_;
            conn_ref.conn_type < ANJAY_CONNECTION_LIMIT_;
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
                                anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    if (server->data_active.needs_reload || server_needs_reconnect(server)) {
        server->data_active.needs_reload = false;
        if (_anjay_active_server_refresh(anjay, server, false)) {
            goto connection_failure;
        }
    }

    if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
        if (!_anjay_server_registration_connection_valid(server)
                || _anjay_server_registration_expired(server)) {
            int result = _anjay_server_ensure_valid_registration(anjay, server);
            if (result < 0) {
                return result;
            } else if (result) {
                goto connection_failure;
            }
        } else {
            _anjay_observe_sched_flush(anjay, (anjay_connection_key_t) {
                .ssid = server->ssid,
                .type = server->data_active.registration_info.conn_type
            });
        }
    }
    return 0;
connection_failure:
    _anjay_server_deactivate(anjay, server->ssid, AVS_TIME_DURATION_ZERO);
    return 0;
}

static int reload_server_by_ssid(anjay_t *anjay,
                                 anjay_servers_t *old_servers,
                                 anjay_ssid_t ssid) {
    anjay_log(TRACE, "reloading server SSID %u", ssid);

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(old_servers, ssid);
    if (server_ptr) {
        anjay_log(TRACE, "reloading inactive server SSID %u", ssid);
        AVS_LIST(anjay_server_info_t) server = AVS_LIST_DETACH(server_ptr);
        _anjay_servers_add(anjay->servers, server);
        if (_anjay_server_active(server)) {
            anjay_log(TRACE, "reloading active server SSID %u", ssid);
            return reload_active_server(anjay, server);
        } else {
            return 0;
        }
    }

    anjay_log(TRACE, "creating server SSID %u", ssid);
    AVS_LIST(anjay_server_info_t) new_server =
            _anjay_servers_create_inactive(ssid);
    if (!new_server) {
        return -1;
    }

    _anjay_servers_add(anjay->servers, new_server);
    return _anjay_server_sched_activate(anjay, new_server,
                                        AVS_TIME_DURATION_ZERO);
}

typedef struct {
    anjay_servers_t *old_servers;
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

    if (reload_server_by_ssid(anjay, state->old_servers, ssid)) {
        anjay_log(TRACE, "could not reload server SSID %u", ssid);
        state->retval = -1;
    }

    return 0;
}

static void reload_servers_sched_job(anjay_t *anjay, void *unused) {
    (void)unused;
    anjay_log(TRACE, "reloading servers");

    anjay_servers_t old_servers = *anjay->servers;
    memset(anjay->servers, 0, sizeof(*anjay->servers));
    reload_servers_state_t reload_state = {
        .old_servers = &old_servers,
        .retval = 0
    };

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (obj && (_anjay_dm_foreach_instance(anjay, obj,
                                           reload_server_by_security_iid,
                                           &reload_state)
                    || reload_state.retval)) {
        // re-add old servers, don't discard them
        AVS_LIST(anjay_server_info_t) *server_ptr;
        AVS_LIST(anjay_server_info_t) helper;
        AVS_LIST_DELETABLE_FOREACH_PTR(server_ptr, helper,
                                       &old_servers.servers) {
            if (_anjay_server_active(*server_ptr)) {
                _anjay_servers_add(anjay->servers,
                                   AVS_LIST_DETACH(server_ptr));
            }
        }
        anjay_log(ERROR, "reloading servers failed, re-scheduling job");
        _anjay_schedule_delayed_reload_servers(anjay);
    } else {
        if (obj) {
            anjay_log(INFO, "servers reloaded");
        } else {
            anjay_log(WARNING,
                      "Security object not present, no servers to create");
        }
        _anjay_observe_gc(anjay);
    }

    _anjay_servers_internal_deregister(anjay, &old_servers);
    _anjay_servers_internal_cleanup(anjay, &old_servers);
    anjay_log(TRACE, "%lu servers reloaded",
              (unsigned long)AVS_LIST_SIZE(anjay->servers->servers));
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
