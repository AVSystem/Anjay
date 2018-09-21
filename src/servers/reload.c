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

#include "../dm/query.h"
#include "../servers_utils.h"

#define ANJAY_SERVERS_INTERNALS

#include "activate.h"
#include "register_internal.h"
#include "reload.h"
#include "server_connections.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static int reload_active_server(anjay_t *anjay, anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    anjay_conn_session_token_t previous_session_token =
            _anjay_server_primary_session_token(server);
    if (server->ssid != ANJAY_SSID_BOOTSTRAP
            && _anjay_server_registration_expired(server)) {
        // Registration expired - we need to re-register, but we only call
        // Register from activate_server, so we need to deactivate first.
        goto deactivate;
    }

    if (_anjay_active_server_refresh(anjay, server)) {
        goto deactivate;
    }

    if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        if (!_anjay_conn_session_tokens_equal(
                    previous_session_token,
                    _anjay_server_primary_session_token(server))) {
            return _anjay_bootstrap_update_reconnected(anjay);
        }
    } else {
        if (_anjay_server_registration_expired(server)
                || !_anjay_server_primary_connection_valid(server)) {
            goto deactivate;
        }
        _anjay_observe_sched_flush(
                anjay, (anjay_connection_key_t) {
                           .ssid = server->ssid,
                           .type = _anjay_server_primary_conn_type(server)
                       });
    }
    return 0;
deactivate:
    _anjay_server_deactivate(anjay, server->ssid, AVS_TIME_DURATION_ZERO);
    return 0;
}

static void reload_server_by_ssid_job(anjay_t *anjay, const void *ssid_ptr) {
    anjay_ssid_t ssid = *(const anjay_ssid_t *) ssid_ptr;
    assert(ssid != ANJAY_SSID_ANY);

    AVS_LIST(anjay_server_info_t) server =
            _anjay_servers_find_active(anjay, ssid);
    if (server && reload_active_server(anjay, server)
            && _anjay_servers_schedule_next_retryable(
                       anjay->sched, server, reload_server_by_ssid_job, ssid)) {
        anjay_log(ERROR, "could not reschedule reload_server_by_ssid_job");
    }
}

static int reload_server_by_ssid(anjay_t *anjay,
                                 anjay_servers_t *old_servers,
                                 anjay_ssid_t ssid) {
    anjay_log(TRACE, "reloading server SSID %u", ssid);

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(old_servers, ssid);
    if (server_ptr) {
        AVS_LIST(anjay_server_info_t) server = AVS_LIST_DETACH(server_ptr);
        _anjay_servers_add(anjay->servers, server);
        if (_anjay_server_active(server)) {
            anjay_log(TRACE, "reloading active server SSID %u", ssid);
            return reload_active_server(anjay, server);
        } else if (!server->next_action_handle
                   && avs_time_real_valid(server->reactivate_time)) {
            return _anjay_server_sched_activate(
                    anjay, server,
                    avs_time_real_diff(server->reactivate_time,
                                       avs_time_real_now()));
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
    int result = 0;
    if (ssid != ANJAY_SSID_BOOTSTRAP
            || _anjay_bootstrap_server_initiated_allowed(anjay)) {
        result = _anjay_server_sched_activate(anjay, new_server,
                                              AVS_TIME_DURATION_ZERO);
    }
    return result;
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

static void reload_servers_sched_job(anjay_t *anjay, const void *unused) {
    (void) unused;
    anjay_log(TRACE, "reloading servers");

    anjay_servers_t old_servers = *anjay->servers;
    memset(anjay->servers, 0, sizeof(*anjay->servers));
    reload_servers_state_t reload_state = {
        .old_servers = &old_servers,
        .retval = 0
    };

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (obj
            && _anjay_dm_foreach_instance(
                       anjay, obj, reload_server_by_security_iid, &reload_state)
            && !reload_state.retval) {
        reload_state.retval = -1;
    }

    // If the only entry we have is a bootstrap server that's inactive and not
    // scheduled for activation - schedule that. It's necessary to perform
    // Client-Initiated Bootstrap if Server-Initiated Bootstrap is disabled in
    // configuration.
    if (!reload_state.retval && anjay->servers->servers
            && !AVS_LIST_NEXT(anjay->servers->servers)
            && anjay->servers->servers->ssid == ANJAY_SSID_BOOTSTRAP
            && !_anjay_server_active(anjay->servers->servers)
            && !anjay->servers->servers->next_action_handle
            && !anjay->servers->servers->reactivate_failed) {
        reload_state.retval =
                _anjay_server_sched_activate(anjay, anjay->servers->servers,
                                             AVS_TIME_DURATION_ZERO);
    }

    if (reload_state.retval) {
        // re-add old servers, don't discard them
        AVS_LIST(anjay_server_info_t) *server_ptr;
        AVS_LIST(anjay_server_info_t) helper;
        AVS_LIST_DELETABLE_FOREACH_PTR(server_ptr, helper,
                                       &old_servers.servers) {
            if (_anjay_server_active(*server_ptr)) {
                _anjay_servers_add(anjay->servers, AVS_LIST_DETACH(server_ptr));
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
              (unsigned long) AVS_LIST_SIZE(anjay->servers->servers));
}

static int schedule_reload_servers(anjay_t *anjay, bool delayed) {
    static const long RELOAD_DELAY_S = 5;
    _anjay_sched_del(anjay->sched, &anjay->reload_servers_sched_job_handle);
    if (_anjay_sched(anjay->sched, &anjay->reload_servers_sched_job_handle,
                     avs_time_duration_from_scalar(delayed ? RELOAD_DELAY_S : 0,
                                                   AVS_TIME_S),
                     reload_servers_sched_job, NULL, 0)) {
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

int _anjay_schedule_reload_server(anjay_t *anjay, anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    _anjay_sched_del(anjay->sched, &server->next_action_handle);
    if (_anjay_servers_schedule_first_retryable(
                anjay->sched, server, AVS_TIME_DURATION_ZERO,
                reload_server_by_ssid_job, server->ssid)) {
        anjay_log(ERROR, "could not schedule reload_server_by_ssid_job");
        return -1;
    }
    return 0;
}

/**
 * Schedules reconnection of all servers, and even downloader sockets. This is
 * basically:
 *
 * - The same as _anjay_schedule_server_reconnect() but for all servers at once,
 *   See the docs there for details.
 * - Exits offline mode if it is currently enabled
 * - Reschedules activation (calls _anjay_server_sched_activate()) for all
 *   servers that have reached the ICMP failure limit
 * - Calls _anjay_downloader_sched_reconnect_all() to reconnect downloader
 *   sockets
 */
int anjay_schedule_reconnect(anjay_t *anjay) {
    int result = _anjay_schedule_reload_servers(anjay);
    if (result) {
        return result;
    }
    anjay->offline = false;
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, anjay->servers->servers) {
        _anjay_connection_suspend((anjay_connection_ref_t) {
            .server = server,
            .conn_type = ANJAY_CONNECTION_UNSET
        });
    }
    result = _anjay_servers_sched_reactivate_all_given_up(anjay);
#ifdef WITH_DOWNLOADER
    if (!result) {
        result = _anjay_downloader_sched_reconnect_all(&anjay->downloader);
    }
#endif // WITH_DOWNLOADER
    return result;
}
