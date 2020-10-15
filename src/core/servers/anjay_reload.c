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

#include <inttypes.h>

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_servers_inactive.h"
#include "../dm/anjay_query.h"

#include "anjay_activate.h"
#include "anjay_register.h"
#include "anjay_reload.h"
#include "anjay_server_connections.h"
#include "anjay_servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static void refresh_server_job(avs_sched_t *sched, const void *server_ptr) {
    anjay_t *anjay = _anjay_get_from_sched(sched);
    anjay_server_info_t *server = *(anjay_server_info_t *const *) server_ptr;

    if (server->ssid != ANJAY_SSID_BOOTSTRAP
            && _anjay_bootstrap_in_progress(anjay)) {
        anjay_log(TRACE,
                  _("Bootstrap is in progress, not refreshing server "
                    "SSID ") "%" PRIu16,
                  server->ssid);
        return;
    }

    _anjay_active_server_refresh(server);
}

static int reload_server_by_ssid(anjay_t *anjay,
                                 anjay_servers_t *old_servers,
                                 anjay_ssid_t ssid) {
    anjay_log(TRACE, _("reloading server SSID ") "%u", ssid);

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(old_servers, ssid);
    if (server_ptr) {
        AVS_LIST(anjay_server_info_t) server = AVS_LIST_DETACH(server_ptr);
        _anjay_servers_add(anjay->servers, server);
        if (ssid == ANJAY_SSID_BOOTSTRAP
                || !_anjay_bootstrap_in_progress(anjay)) {
            if (_anjay_server_active(server)) {
                anjay_log(TRACE, _("reloading active server SSID ") "%u", ssid);
                return _anjay_schedule_refresh_server(server,
                                                      AVS_TIME_DURATION_ZERO);
            } else if (!server->next_action_handle
                       && avs_time_real_valid(server->reactivate_time)) {
                return _anjay_server_sched_activate(
                        server,
                        avs_time_real_diff(server->reactivate_time,
                                           avs_time_real_now()));
            }
        }
        return 0;
    }

    anjay_log(TRACE, _("creating server SSID ") "%u", ssid);
    AVS_LIST(anjay_server_info_t) new_server =
            _anjay_servers_create_inactive(anjay, ssid);
    if (!new_server) {
        return -1;
    }

    _anjay_servers_add(anjay->servers, new_server);
    int result = 0;
    if ((ssid != ANJAY_SSID_BOOTSTRAP && !_anjay_bootstrap_in_progress(anjay))
            || _anjay_bootstrap_legacy_server_initiated_allowed(anjay)) {
        result = _anjay_server_sched_activate(new_server,
                                              AVS_TIME_DURATION_ZERO);
    }
    return result;
}

typedef struct {
    anjay_servers_t *old_servers;
    int retval;
} reload_servers_state_t;

static int reload_server_by_server_iid(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj,
                                       anjay_iid_t iid,
                                       void *state_) {
    (void) obj;
    reload_servers_state_t *state = (reload_servers_state_t *) state_;

    anjay_ssid_t ssid;
    if (_anjay_ssid_from_server_iid(anjay, iid, &ssid)) {
        state->retval = -1;
        return 0;
    }

    if (reload_server_by_ssid(anjay, state->old_servers, ssid)) {
        anjay_log(TRACE, _("could not reload server SSID ") "%u", ssid);
        state->retval = -1;
    }

    return 0;
}

static void reload_servers_sched_job(avs_sched_t *sched, const void *unused) {
    (void) unused;
    anjay_log(TRACE, _("reloading servers"));

    anjay_t *anjay = _anjay_get_from_sched(sched);
    anjay_servers_t old_servers = *anjay->servers;
    memset(anjay->servers, 0, sizeof(*anjay->servers));
    reload_servers_state_t reload_state = {
        .old_servers = &old_servers,
        .retval = 0
    };

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SERVER);
    if (obj
            && _anjay_dm_foreach_instance(
                       anjay, obj, reload_server_by_server_iid, &reload_state)
            && !reload_state.retval) {
        reload_state.retval = -1;
    }
    if (!reload_state.retval
            && _anjay_find_bootstrap_security_iid(anjay) != ANJAY_ID_INVALID) {
        reload_state.retval = reload_server_by_ssid(anjay, &old_servers,
                                                    ANJAY_SSID_BOOTSTRAP);
    }

    // If the only entry we have is a bootstrap server that's inactive and not
    // scheduled for activation - schedule that. It's necessary to perform
    // Client-Initiated Bootstrap if 1.0-style Server-Initiated Bootstrap is
    // disabled in configuration.
    if (!reload_state.retval && anjay->servers->servers
            && !AVS_LIST_NEXT(anjay->servers->servers)
            && anjay->servers->servers->ssid == ANJAY_SSID_BOOTSTRAP
            && !_anjay_server_active(anjay->servers->servers)
            && !anjay->servers->servers->next_action_handle
            && !anjay->servers->servers->refresh_failed) {
        reload_state.retval =
                _anjay_server_sched_activate(anjay->servers->servers,
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
        anjay_log(WARNING, _("reloading servers failed, re-scheduling job"));
        _anjay_schedule_delayed_reload_servers(anjay);
    } else {
        if (obj) {
            anjay_log(INFO, _("servers reloaded"));
        } else {
            anjay_log(WARNING,
                      _("Security object not present, no servers to create"));
        }
        _anjay_observe_gc(anjay);
    }

    _anjay_servers_internal_deregister(&old_servers);
    _anjay_servers_internal_cleanup(&old_servers);
    anjay_log(TRACE, "%lu" _(" servers reloaded"),
              (unsigned long) AVS_LIST_SIZE(anjay->servers->servers));
}

static int schedule_reload_servers(anjay_t *anjay, bool delayed) {
    static const long RELOAD_DELAY_S = 5;
    if (!anjay->sched
            || AVS_SCHED_DELAYED(
                       anjay->sched, &anjay->reload_servers_sched_job_handle,
                       avs_time_duration_from_scalar(
                               delayed ? RELOAD_DELAY_S : 0, AVS_TIME_S),
                       reload_servers_sched_job, NULL, 0)) {
        anjay_log(ERROR, _("could not schedule reload_servers_job"));
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

int _anjay_schedule_refresh_server(anjay_server_info_t *server,
                                   avs_time_duration_t delay) {
    if (AVS_SCHED_DELAYED(server->anjay->sched, &server->next_action_handle,
                          delay, refresh_server_job, &server, sizeof(server))) {
        anjay_log(ERROR, _("could not schedule refresh_server_job"));
        return -1;
    }
    return 0;
}

const anjay_transport_set_t ANJAY_TRANSPORT_SET_ALL = {
    .udp = true,
    .tcp = true
};

const anjay_transport_set_t ANJAY_TRANSPORT_SET_IP = {
    .udp = true,
    .tcp = true
};

const anjay_transport_set_t ANJAY_TRANSPORT_SET_UDP = {
    .udp = true
};

const anjay_transport_set_t ANJAY_TRANSPORT_SET_TCP = {
    .tcp = true
};

static anjay_transport_set_t transport_set_not(anjay_transport_set_t set) {
    return (anjay_transport_set_t) {
        .udp = !set.udp,
        .tcp = !set.tcp
    };
}

static anjay_transport_set_t transport_set_union(anjay_transport_set_t left,
                                                 anjay_transport_set_t right) {
    return (anjay_transport_set_t) {
        .udp = left.udp || right.udp,
        .tcp = left.tcp || right.tcp
    };
}

static anjay_transport_set_t
transport_set_intersection(anjay_transport_set_t left,
                           anjay_transport_set_t right) {
    return (anjay_transport_set_t) {
        .udp = left.udp && right.udp,
        .tcp = left.tcp && right.tcp
    };
}

static bool transport_set_empty(anjay_transport_set_t set) {
    return !(set.udp || set.tcp);
}

anjay_transport_set_t
_anjay_transport_set_remove_unavailable(anjay_t *anjay,
                                        anjay_transport_set_t set) {
    (void) anjay;
    return (anjay_transport_set_t) {
        .udp = set.udp,
        .tcp = set.tcp
    };
}

bool _anjay_socket_transport_included(anjay_transport_set_t set,
                                      anjay_socket_transport_t transport) {
    switch (transport) {
    case ANJAY_SOCKET_TRANSPORT_UDP:
        return set.udp;
    case ANJAY_SOCKET_TRANSPORT_TCP:
        return set.tcp;
    }
    AVS_UNREACHABLE("Invalid transport");
    return false;
}

bool _anjay_socket_transport_is_online(anjay_t *anjay,
                                       anjay_socket_transport_t transport) {
    return _anjay_socket_transport_included(anjay->online_transports,
                                            transport);
}

bool anjay_transport_is_offline(anjay_t *anjay,
                                anjay_transport_set_t transport_set) {
    return transport_set_empty(transport_set_intersection(
            anjay->online_transports, transport_set));
}

int anjay_transport_enter_offline(anjay_t *anjay,
                                  anjay_transport_set_t transport_set) {
    return anjay_transport_set_online(
            anjay,
            transport_set_intersection(anjay->online_transports,
                                       transport_set_not(transport_set)));
}

int anjay_transport_exit_offline(anjay_t *anjay,
                                 anjay_transport_set_t transport_set) {
    return anjay_transport_set_online(
            anjay,
            transport_set_union(anjay->online_transports, transport_set));
}

int anjay_transport_set_online(anjay_t *anjay,
                               anjay_transport_set_t transport_set) {
    anjay_transport_set_t orig_online_transports = anjay->online_transports;
    anjay->online_transports =
            _anjay_transport_set_remove_unavailable(anjay, transport_set);
    bool reload_was_scheduled = !!anjay->reload_servers_sched_job_handle;
    int result = _anjay_schedule_reload_servers(anjay);
#ifdef ANJAY_WITH_DOWNLOADER
    if (!result
            && (result = _anjay_downloader_sync_online_transports(
                        &anjay->downloader))
            && !reload_was_scheduled) {
        avs_sched_del(&anjay->reload_servers_sched_job_handle);
    }
#else  // ANJAY_WITH_DOWNLOADER
    (void) reload_was_scheduled;
#endif // ANJAY_WITH_DOWNLOADER
    if (!result) {
        _anjay_servers_interrupt_offline(anjay);
    } else {
        anjay->online_transports = orig_online_transports;
    }
    return result;
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
int anjay_transport_schedule_reconnect(anjay_t *anjay,
                                       anjay_transport_set_t transport_set) {
    int result = anjay_transport_exit_offline(anjay, transport_set);
    if (result) {
        return result;
    }
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, anjay->servers->servers) {
        anjay_connection_type_t conn_type;
        ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
            const anjay_connection_ref_t ref = {
                .server = server,
                .conn_type = conn_type
            };
            anjay_server_connection_t *connection =
                    _anjay_get_server_connection(ref);
            if (_anjay_connection_internal_get_socket(connection)
                    && _anjay_socket_transport_included(
                               transport_set, connection->transport)) {
                _anjay_connection_suspend(ref);
            }
        }
    }
    result = _anjay_servers_sched_reactivate_all_given_up(anjay);
#ifdef ANJAY_WITH_DOWNLOADER
    if (!result) {
        result = _anjay_downloader_sched_reconnect(&anjay->downloader,
                                                   transport_set);
    }
#endif // ANJAY_WITH_DOWNLOADER
    return result;
}

void _anjay_security_config_cache_cleanup(
        anjay_security_config_cache_t *cache) {
    avs_free(cache->client_key);
    avs_free(cache->client_cert_array);
    avs_free(cache->cert_revocation_lists_array);
    avs_free(cache->trusted_certs_array);
    avs_free(cache->psk_buffer);
    avs_free(cache->dane_tlsa_record);
    avs_free(cache->ciphersuites.ids);
    memset(cache, 0, sizeof(*cache));
}
