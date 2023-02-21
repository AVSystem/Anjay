/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <inttypes.h>

#include <anjay_modules/anjay_time_defs.h>

#include <avsystem/commons/avs_memory.h>

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_core.h"
#include "../anjay_servers_inactive.h"
#include "../anjay_servers_private.h"
#include "../anjay_servers_utils.h"
#include "../dm/anjay_query.h"

#include "anjay_activate.h"
#include "anjay_register.h"
#include "anjay_server_connections.h"
#include "anjay_servers_internal.h"

VISIBILITY_SOURCE_BEGIN

void _anjay_server_clean_active_data(anjay_server_info_t *server) {
    avs_sched_del(&server->next_action_handle);
    _anjay_registration_exchange_state_cleanup(
            &server->registration_exchange_state);
    _anjay_connections_close(server->anjay, &server->connections);
}

void _anjay_server_cleanup(anjay_server_info_t *server) {
    anjay_log(TRACE, _("clear_server SSID ") "%u", server->ssid);

    // defined(ANJAY_WITH_CORE_PERSISTENCE)
    _anjay_server_clean_active_data(server);
    _anjay_registration_info_cleanup(&server->registration_info);
}

#ifndef ANJAY_WITHOUT_DEREGISTER
void _anjay_servers_internal_deregister(
        AVS_LIST(anjay_server_info_t) *servers) {
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, *servers) {
        if (_anjay_server_active(server) && server->ssid != ANJAY_SSID_BOOTSTRAP
                && !_anjay_server_registration_expired(server)) {
            _anjay_server_deregister(server);
        }
    }
}
#endif // ANJAY_WITHOUT_DEREGISTER

void _anjay_servers_internal_cleanup(AVS_LIST(anjay_server_info_t) *servers) {
    anjay_log(TRACE, _("cleaning up ") "%lu" _(" servers"),
              (unsigned long) AVS_LIST_SIZE(*servers));

    AVS_LIST_CLEAR(servers) {
        _anjay_server_cleanup(*servers);
    }
}

#ifndef ANJAY_WITHOUT_DEREGISTER
void _anjay_servers_deregister(anjay_unlocked_t *anjay) {
    _anjay_servers_internal_deregister(&anjay->servers);
}
#endif // ANJAY_WITHOUT_DEREGISTER

void _anjay_servers_cleanup(anjay_unlocked_t *anjay) {
    _anjay_servers_internal_cleanup(&anjay->servers);
    AVS_LIST_CLEAR(&anjay->cached_public_sockets);
}

void _anjay_servers_cleanup_inactive_nonbootstrap(anjay_unlocked_t *anjay) {
    AVS_LIST(anjay_server_info_t) *server_ptr;
    AVS_LIST(anjay_server_info_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(server_ptr, helper, &anjay->servers) {
        if ((*server_ptr)->ssid != ANJAY_SSID_BOOTSTRAP
                && !_anjay_server_active(*server_ptr)) {
            _anjay_server_cleanup(*server_ptr);
            AVS_LIST_DELETE(server_ptr);
        }
    }
}

avs_coap_ctx_t *_anjay_connection_get_coap(anjay_connection_ref_t ref) {
    assert(ref.server);
    return _anjay_get_server_connection(ref)->coap_ctx;
}

avs_net_socket_t *
_anjay_connection_get_online_socket(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    if (!connection || !_anjay_connection_is_online(connection)) {
        return NULL;
    }
    return _anjay_connection_internal_get_socket(connection);
}

bool _anjay_connection_ready_for_outgoing_message(anjay_connection_ref_t ref) {
    // It is now possible for the socket to exist and be connected even though
    // the server has no valid registration. This may happen during the
    // _anjay_connection_internal_bring_online() backoff. We don't want to send
    // notifications if we don't have a valid registration, so we treat such
    // server as inactive for notification purposes.
    anjay_unlocked_t *anjay = _anjay_from_server(ref.server);
    return !_anjay_bootstrap_in_progress(anjay)
           && _anjay_server_active(ref.server)
           && !_anjay_server_registration_expired(ref.server)
           && !_anjay_server_registration_info(ref.server)->update_forced;
}

static int add_socket_onto_list(AVS_LIST(anjay_socket_entry_t) *tail_ptr,
                                avs_net_socket_t *socket,
                                anjay_socket_transport_t transport,
                                anjay_ssid_t ssid,
                                bool queue_mode) {
    assert(!*tail_ptr);
    AVS_LIST_INSERT_NEW(anjay_socket_entry_t, tail_ptr);
    if (!*tail_ptr) {
        anjay_log(ERROR, _("Out of memory while building socket list"));
        return -1;
    }
    (*tail_ptr)->socket = socket;
    (*tail_ptr)->transport = transport;
    (*tail_ptr)->ssid = ssid;
    (*tail_ptr)->queue_mode = queue_mode;
    return 0;
}

AVS_LIST(const anjay_socket_entry_t)
_anjay_collect_socket_entries(anjay_unlocked_t *anjay, bool include_offline) {
    AVS_LIST(anjay_socket_entry_t) result = NULL;
    AVS_LIST(anjay_socket_entry_t) *tail_ptr = &result;

    anjay_connection_ref_t ref;
    AVS_LIST_FOREACH(ref.server, anjay->servers) {
        ref.conn_type = ANJAY_CONNECTION_PRIMARY;
        anjay_server_connection_t *conn = _anjay_get_server_connection(ref);
        assert(conn);
        avs_net_socket_t *socket = _anjay_connection_internal_get_socket(conn);
        if (socket && (include_offline || _anjay_socket_is_online(socket))) {
            if (!add_socket_onto_list(
                        tail_ptr, socket, conn->transport, ref.server->ssid,
                        ref.server->registration_info.queue_mode)) {
                AVS_LIST_ADVANCE_PTR(&tail_ptr);
            }
        }
    }

#ifdef ANJAY_WITH_DOWNLOADER
    _anjay_downloader_get_sockets(&anjay->downloader, tail_ptr,
                                  include_offline);
#endif // ANJAY_WITH_DOWNLOADER
    return result;
}

AVS_LIST(const anjay_socket_entry_t)
anjay_get_socket_entries(anjay_t *anjay_locked) {
    AVS_LIST(const anjay_socket_entry_t) result = NULL;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    AVS_LIST_CLEAR(&anjay->cached_public_sockets);
    anjay->cached_public_sockets =
            _anjay_collect_socket_entries(anjay, /* include_offline = */ false);
    result = anjay->cached_public_sockets;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_insert_ptr(AVS_LIST(anjay_server_info_t) *servers,
                               anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) *it;
    AVS_LIST_FOREACH_PTR(it, servers) {
        if ((*it)->ssid >= ssid) {
            return it;
        }
    }
    return it;
}

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_ptr(AVS_LIST(anjay_server_info_t) *servers,
                        anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) *ptr =
            _anjay_servers_find_insert_ptr(servers, ssid);
    if (*ptr && (*ptr)->ssid == ssid) {
        return ptr;
    }

    anjay_log(TRACE, _("no server with SSID ") "%u", ssid);
    return NULL;
}

anjay_server_info_t *_anjay_servers_find(anjay_unlocked_t *anjay,
                                         anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) *ptr =
            _anjay_servers_find_ptr(&anjay->servers, ssid);
    return ptr ? *ptr : NULL;
}

bool _anjay_server_is_disable_scheduled(anjay_server_info_t *server) {
    return server->next_action_handle
           && (server->next_action
                       == ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_TIMEOUT_FROM_DM
               || server->next_action
                          == ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_EXPLICIT_TIMEOUT);
}

bool _anjay_server_active(anjay_server_info_t *server) {
    if (_anjay_server_is_disable_scheduled(server)) {
        return false;
    }
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        if (_anjay_connection_internal_get_socket(
                    _anjay_get_server_connection((anjay_connection_ref_t) {
                        .server = server,
                        .conn_type = conn_type
                    }))) {
            return true;
        }
    }

    return false;
}

anjay_unlocked_t *_anjay_from_server(anjay_server_info_t *server) {
    return server->anjay;
}

anjay_ssid_t _anjay_server_ssid(anjay_server_info_t *server) {
    return server->ssid;
}

anjay_iid_t _anjay_server_last_used_security_iid(anjay_server_info_t *server) {
    return server->last_used_security_iid;
}

const anjay_binding_mode_t *
_anjay_server_binding_mode(anjay_server_info_t *server) {
    return (const anjay_binding_mode_t *) &server->binding_mode;
}

#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
void _anjay_server_set_last_communication_time(anjay_server_info_t *server) {
    server->last_communication_time = avs_time_real_now();
    anjay_log(TRACE,
              _("Update server (SSID: ") "%d" _(
                      ") last communication time to ") "%s",
              server->ssid,
              AVS_TIME_DURATION_AS_STRING(
                      server->last_communication_time.since_real_epoch));
}
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API

int _anjay_servers_foreach_ssid(anjay_unlocked_t *anjay,
                                anjay_servers_foreach_ssid_handler_t *handler,
                                void *data) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers) {
        int result = handler(anjay, it->ssid, data);
        if (result == ANJAY_FOREACH_BREAK) {
            anjay_log(DEBUG, _("servers_foreach_ssid: break on ") "%u",
                      it->ssid);
            return 0;
        } else if (result) {
            anjay_log(WARNING,
                      _("servers_foreach_ssid handler failed for ") "%u" _(
                              " (") "%d" _(")"),
                      it->ssid, result);
            return result;
        }
    }

    return 0;
}

int _anjay_servers_foreach_active(anjay_unlocked_t *anjay,
                                  anjay_servers_foreach_handler_t *handler,
                                  void *data) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers) {
        if (!_anjay_server_active(it)) {
            continue;
        }
        int result = handler(anjay, it, data);
        if (result == ANJAY_FOREACH_BREAK) {
            anjay_log(DEBUG, _("servers_foreach_ssid: break on ") "%u",
                      it->ssid);
            return 0;
        } else if (result) {
            anjay_log(WARNING,
                      _("servers_foreach_ssid handler failed for ") "%u" _(
                              " (") "%d" _(")"),
                      it->ssid, result);
            return result;
        }
    }

    return 0;
}

#if defined(ANJAY_WITH_LWM2M11)
bool _anjay_bootstrap_server_exists(anjay_unlocked_t *anjay) {
    AVS_STATIC_ASSERT(ANJAY_SSID_BOOTSTRAP == UINT16_MAX,
                      bootstrap_server_is_last);
    AVS_LIST(anjay_server_info_t) candidate = AVS_LIST_TAIL(anjay->servers);
    return candidate && candidate->ssid == ANJAY_SSID_BOOTSTRAP;
}
#endif // defined(ANJAY_WITH_LWM2M11) || defined(ANJAY_WITH_EST)

static void server_next_action_job(avs_sched_t *sched, const void *server_ptr) {
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    anjay_server_info_t *server = *(anjay_server_info_t *const *) server_ptr;
    switch (server->next_action) {
    case ANJAY_SERVER_NEXT_ACTION_COMMUNICATION_ERROR:
        _anjay_server_on_failure(server, "not reachable");
        goto success;

    case ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_TIMEOUT_FROM_DM:
        _anjay_disable_server_with_timeout_from_dm_sync(server);
        goto success;

    case ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_EXPLICIT_TIMEOUT:
        _anjay_disable_server_with_explicit_timeout_sync(server);
        goto success;

    case ANJAY_SERVER_NEXT_ACTION_SEND_UPDATE:
        server->registration_info.update_forced = true;
        _anjay_active_server_refresh(server);
        goto success;

    case ANJAY_SERVER_NEXT_ACTION_REFRESH:
        if (server->ssid != ANJAY_SSID_BOOTSTRAP
                && _anjay_bootstrap_in_progress(anjay)) {
            anjay_log(TRACE,
                      _("Bootstrap is in progress, not refreshing server "
                        "SSID ") "%" PRIu16,
                      server->ssid);
            // NOTE: Bootstrap Finish will trigger
            // _anjay_schedule_reload_servers(), server will be refreshed then.
        } else {
            _anjay_active_server_refresh(server);
        }
        goto success;
    }
    AVS_UNREACHABLE("Invalid next_action enum value");
success:;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

int _anjay_server_reschedule_next_action(
        anjay_server_info_t *server,
        avs_time_duration_t delay,
        anjay_server_next_action_t next_action) {
    if (avs_time_duration_less(delay, AVS_TIME_DURATION_ZERO)) {
        // Ensure that the job won't execute before already scheduled jobs
        delay = AVS_TIME_DURATION_ZERO;
    }
    int result;
    if (server->next_action_handle) {
        result = AVS_RESCHED_DELAYED(&server->next_action_handle, delay);
    } else {
        result = AVS_SCHED_DELAYED(server->anjay->sched,
                                   &server->next_action_handle, delay,
                                   server_next_action_job, &server,
                                   sizeof(server));
    }
    if (!result) {
        server->next_action = next_action;
    }
    return result;
}
