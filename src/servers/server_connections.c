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

#include <inttypes.h>

#include <avsystem/commons/stream/stream_net.h>
#include <avsystem/commons/utils.h>

#include "../dm/query.h"
#include "../servers_utils.h"
#include "../utils_core.h"

#define ANJAY_SERVERS_INTERNALS

#include "activate.h"
#include "reload.h"
#include "server_connections.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static int read_binding_mode(anjay_t *anjay,
                             anjay_ssid_t ssid,
                             anjay_binding_mode_t *out_binding_mode) {
    anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, ANJAY_IID_INVALID,
                               ANJAY_DM_RID_SERVER_BINDING);

    if (_anjay_find_server_iid(anjay, ssid, &path.iid)
            || _anjay_dm_res_read_string(anjay, &path, *out_binding_mode,
                                         sizeof(*out_binding_mode))) {
        anjay_log(WARNING, "could not read binding mode for LwM2M server %u",
                  ssid);
        return -1;
    }
    if (!anjay_binding_mode_valid(*out_binding_mode)) {
        anjay_log(WARNING, "invalid binding mode \"%s\" for LwM2M server %u",
                  *out_binding_mode, ssid);
        return -1;
    }
    return 0;
}

anjay_server_connection_mode_t
_anjay_connection_current_mode(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    if (_anjay_connection_internal_get_socket(connection)) {
        return connection->mode;
    } else {
        return ANJAY_CONNECTION_DISABLED;
    }
}

anjay_conn_session_token_t
_anjay_server_primary_session_token(anjay_server_info_t *server) {
    return _anjay_connections_get_primary_session_token(&server->connections);
}

static int read_server_uri(anjay_t *anjay,
                           anjay_iid_t security_iid,
                           anjay_url_t *out_uri) {
    char raw_uri[ANJAY_MAX_URL_RAW_LENGTH];

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_SERVER_URI);

    if (_anjay_dm_res_read_string(anjay, &path, raw_uri, sizeof(raw_uri))) {
        anjay_log(ERROR, "could not read LwM2M server URI");
        return -1;
    }

    anjay_url_t uri = ANJAY_URL_EMPTY;
    if (_anjay_url_parse(raw_uri, &uri)) {
        _anjay_url_cleanup(&uri);
        anjay_log(ERROR, "could not parse LwM2M server URI: %s", raw_uri);
        return -1;
    }
    if (!*uri.port) {
        switch (uri.protocol) {
        case ANJAY_URL_PROTOCOL_COAP:
            strcpy(uri.port, "5683");
            break;
        case ANJAY_URL_PROTOCOL_COAPS:
            strcpy(uri.port, "5684");
            break;
        }
    }
    *out_uri = uri;
    return 0;
}

void _anjay_active_server_refresh(anjay_t *anjay, anjay_server_info_t *server) {
    anjay_log(TRACE, "refreshing SSID %u", server->ssid);

    int result = -1;
    anjay_iid_t security_iid;
    if (_anjay_find_security_iid(anjay, server->ssid, &security_iid)) {
        anjay_log(ERROR, "could not find server Security IID");
    } else {
        anjay_url_t uri;
        if (!read_server_uri(anjay, security_iid, &uri)) {
            anjay_binding_mode_t binding_mode;
            if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
                result = avs_simple_snprintf(
                        binding_mode, sizeof(binding_mode), "%s",
                        _anjay_sms_router(anjay) ? "US" : "U");
                result = AVS_MIN(result, 0);
            } else {
                result = read_binding_mode(anjay, server->ssid, &binding_mode);
            }
            if (!result) {
                _anjay_connections_refresh(anjay,
                                           &server->connections,
                                           security_iid,
                                           &uri,
                                           binding_mode);
            }
            _anjay_url_cleanup(&uri);
        }
    }
    if (result) {
        _anjay_server_on_refreshed(anjay, server,
                                   ANJAY_SERVER_CONNECTION_ERROR);
    }
}

static void connection_suspend(anjay_connection_ref_t conn_ref) {
    avs_net_abstract_socket_t *socket = _anjay_connection_internal_get_socket(
            _anjay_get_server_connection(conn_ref));
    if (socket) {
        avs_net_socket_close(socket);
    }
}

void _anjay_connection_suspend(anjay_connection_ref_t conn_ref) {
    if (conn_ref.conn_type == ANJAY_CONNECTION_UNSET) {
        ANJAY_CONNECTION_TYPE_FOREACH(conn_ref.conn_type) {
            connection_suspend(conn_ref);
        }
    } else {
        connection_suspend(conn_ref);
    }
}

void _anjay_connection_mark_stable(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    assert(connection);
    assert(_anjay_connection_is_online(connection));
    connection->state = ANJAY_SERVER_CONNECTION_STABLE;
}

void _anjay_connection_bring_online(anjay_t *anjay,
                                    anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    assert(connection);
    assert(!_anjay_connection_is_online(connection));
    (void) connection;
    _anjay_connection_internal_bring_online(anjay, &ref.server->connections,
                                            ref.conn_type);
}

static void queue_mode_close_socket(anjay_t *anjay, const void *ref_ptr) {
    (void) anjay;
    _anjay_connection_suspend(*(const anjay_connection_ref_t *) ref_ptr);
}

void _anjay_connection_schedule_queue_mode_close(anjay_t *anjay,
                                                 anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    assert(connection);
    assert(_anjay_connection_is_online(connection));

    _anjay_sched_del(anjay->sched, &connection->queue_mode_close_socket_clb);
    if (connection->mode != ANJAY_CONNECTION_QUEUE) {
        return;
    }

    avs_time_duration_t delay = avs_coap_max_transmit_wait(
            _anjay_tx_params_for_conn_type(anjay, ref.conn_type));

    // see comment on field declaration for logic summary
    if (_anjay_sched(anjay->sched, &connection->queue_mode_close_socket_clb,
                     delay, queue_mode_close_socket, &ref, sizeof(ref))) {
        anjay_log(ERROR, "could not schedule queue mode operations");
    }
}

const anjay_url_t *_anjay_connection_uri(anjay_connection_ref_t ref) {
    return &_anjay_get_server_connection(ref)->uri;
}

void _anjay_connections_on_refreshed(anjay_t *anjay,
                                     anjay_connections_t *connections,
                                     anjay_server_connection_state_t state) {
    _anjay_server_on_refreshed(
            anjay,
            AVS_CONTAINER_OF(connections, anjay_server_info_t, connections),
            state);
}

void _anjay_connections_flush_notifications(anjay_t *anjay,
                                            anjay_connections_t *connections) {
    anjay_server_info_t *server =
            AVS_CONTAINER_OF(connections, anjay_server_info_t, connections);
    if (_anjay_connections_get_primary(connections) == ANJAY_CONNECTION_UNSET
            || _anjay_server_registration_expired(server)) {
        anjay_log(TRACE, "Server has no valid registration, "
                         "not flushing notifications");
        return;
    }

    anjay_connection_key_t key;
    key.ssid = server->ssid;
    ANJAY_CONNECTION_TYPE_FOREACH(key.type) {
        anjay_server_connection_t *connection =
                _anjay_connection_get(connections, key.type);
        if (connection->needs_observe_flush
                && _anjay_connection_is_online(connection)
                && (key.ssid == ANJAY_SSID_BOOTSTRAP
                    || !_anjay_observe_sched_flush(anjay, key))) {
            connection->needs_observe_flush = false;
        }
    }
}
