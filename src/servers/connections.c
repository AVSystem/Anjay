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

#include <avsystem/commons/errno.h>
#include <avsystem/commons/utils.h>

#include "../servers_utils.h"

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "connections_internal.h"
#include "reload.h"
#include "server_connections.h"

VISIBILITY_SOURCE_BEGIN

avs_net_abstract_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection) {
    return connection->conn_socket_;
}

void _anjay_connection_internal_clean_socket(
        const anjay_t *anjay, anjay_server_connection_t *connection) {
    avs_net_socket_cleanup(&connection->conn_socket_);
    _anjay_sched_del(anjay->sched, &connection->queue_mode_close_socket_clb);
}

bool _anjay_connection_is_online(anjay_server_connection_t *connection) {
    avs_net_abstract_socket_t *socket =
            _anjay_connection_internal_get_socket(connection);
    if (!socket) {
        return false;
    }
    avs_net_socket_opt_value_t opt;
    if (avs_net_socket_get_opt(socket, AVS_NET_SOCKET_OPT_STATE, &opt)) {
        anjay_log(ERROR, "Could not get socket state");
        return false;
    }
    return opt.state == AVS_NET_SOCKET_STATE_CONNECTED;
}

int _anjay_connection_init_psk_security(avs_net_security_info_t *security,
                                        const anjay_server_dtls_keys_t *keys) {
    *security = avs_net_security_info_from_psk((avs_net_psk_info_t) {
        .psk = keys->secret_key,
        .psk_size = keys->secret_key_size,
        .identity = keys->pk_or_identity,
        .identity_size = keys->pk_or_identity_size
    });
    return 0;
}

static bool has_error(anjay_connections_t *connections) {
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        anjay_server_connection_t *connection =
                _anjay_connection_get(connections, conn_type);
        if (connection->mode != ANJAY_CONNECTION_DISABLED
                && connection->state == ANJAY_SERVER_CONNECTION_ERROR) {
            return true;
        }
    }
    return false;
}

static void on_connection_refreshed(anjay_t *anjay,
                                    anjay_connections_t *connections) {
    bool state_is_stable = true;
    connections->primary_connection = ANJAY_CONNECTION_UNSET;
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        anjay_server_connection_t *connection =
                _anjay_connection_get(connections, conn_type);
        if (connection->state == ANJAY_SERVER_CONNECTION_IN_PROGRESS) {
            state_is_stable = false;
        } else if (connection->mode != ANJAY_CONNECTION_DISABLED
                   && connection->state != ANJAY_SERVER_CONNECTION_ERROR
                   && connections->primary_connection
                              == ANJAY_CONNECTION_UNSET) {
            connections->primary_connection = conn_type;
        }
    }
    if (state_is_stable) {
        anjay_server_connection_state_t state = ANJAY_SERVER_CONNECTION_ERROR;
        if (connections->primary_connection != ANJAY_CONNECTION_UNSET) {
            if (has_error(connections)) {
                // some connection is available, but some other failed
                _anjay_schedule_delayed_reload_servers(anjay);
            }
            state = _anjay_connection_get(connections,
                                          connections->primary_connection)
                            ->state;
        }

        _anjay_connections_on_refreshed(anjay, connections, state);
        _anjay_connections_flush_notifications(anjay, connections);
    }
}

static const anjay_connection_type_definition_t *
get_connection_type_def(anjay_connection_type_t type) {
    switch (type) {
    case ANJAY_CONNECTION_UDP:
        return &ANJAY_CONNECTION_DEF_UDP;
    default:
        AVS_UNREACHABLE("unknown connection type");
        return NULL;
    }
}

void _anjay_connection_internal_bring_online(
        anjay_t *anjay,
        anjay_connections_t *connections,
        anjay_connection_type_t conn_type) {
    assert(connections);
    anjay_server_connection_t *connection =
            _anjay_connection_get(connections, conn_type);
    assert(connection);
    assert(connection->conn_socket_);
    assert(!connection->queue_mode_close_socket_clb);

    if (_anjay_connection_is_online(connection)) {
        anjay_log(INFO, "socket already connected");
        connection->state = ANJAY_SERVER_CONNECTION_STABLE;
        connection->needs_observe_flush = true;
    } else if (get_connection_type_def(conn_type)->connect_socket(anjay,
                                                                  connection)) {
        connection->state = ANJAY_SERVER_CONNECTION_ERROR;
        if (avs_net_socket_close(connection->conn_socket_)) {
            anjay_log(ERROR, "Could not close the socket (?!)");
        }
    } else {
        avs_net_socket_opt_value_t session_resumed;
        if (avs_net_socket_get_opt(connection->conn_socket_,
                                   AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                   &session_resumed)) {
            session_resumed.flag = false;
        }
        if (!session_resumed.flag) {
            _anjay_conn_session_token_reset(&connection->session_token);
        }
        anjay_log(INFO,
                  session_resumed.flag ? "resumed connection" : "reconnected");
        connection->state = ANJAY_SERVER_CONNECTION_FRESHLY_CONNECTED;
        connection->needs_observe_flush = true;
    }
    on_connection_refreshed(anjay, connections);
}

static void connection_cleanup(const anjay_t *anjay,
                               anjay_server_connection_t *connection) {
    _anjay_connection_internal_clean_socket(anjay, connection);
    _anjay_url_cleanup(&connection->uri);
}

void _anjay_connections_close(const anjay_t *anjay,
                              anjay_connections_t *connections) {
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        connection_cleanup(anjay,
                           _anjay_connection_get(connections, conn_type));
    }
}

anjay_connection_type_t
_anjay_connections_get_primary(anjay_connections_t *connections) {
    return connections->primary_connection;
}

anjay_conn_session_token_t
_anjay_connections_get_primary_session_token(anjay_connections_t *connections) {
    anjay_connection_type_t conn_type =
            _anjay_connections_get_primary(connections);
    if (conn_type == ANJAY_CONNECTION_UNSET) {
        anjay_conn_session_token_t result;
        _anjay_conn_session_token_reset(&result);
        return result;
    }
    return _anjay_connection_get(connections, conn_type)->session_token;
}

static int recreate_socket(anjay_t *anjay,
                           const anjay_connection_type_definition_t *def,
                           anjay_server_connection_t *connection,
                           anjay_connection_info_t *inout_info) {
    anjay_server_dtls_keys_t dtls_keys;
    memset(&dtls_keys, 0, sizeof(dtls_keys));

    // At this point, inout_info has "global" settings filled,
    // but transport-specific (i.e. UDP or SMS) fields are not
    if (def->get_connection_info(anjay, inout_info, &dtls_keys)) {
        anjay_log(DEBUG, "could not get %s connection info for server /%u/%u",
                  def->name, ANJAY_DM_OID_SECURITY, inout_info->security_iid);
        return -1;
    }
    assert(!_anjay_connection_internal_get_socket(connection));

    avs_net_ssl_configuration_t socket_config;
    memset(&socket_config, 0, sizeof(socket_config));
    socket_config.backend_configuration = anjay->udp_socket_config;
    socket_config.backend_configuration.reuse_addr = 1;
    socket_config.backend_configuration.preferred_endpoint =
            &connection->nontransient_state.preferred_endpoint;
    socket_config.version = anjay->dtls_version;
    socket_config.session_resumption_buffer =
            connection->nontransient_state.dtls_session_buffer;
    socket_config.session_resumption_buffer_size =
            sizeof(connection->nontransient_state.dtls_session_buffer);
    socket_config.dtls_handshake_timeouts =
            def->get_dtls_handshake_timeouts(anjay);

    int result;
    (void) ((result = def->get_net_security_info(&socket_config.security,
                                                 inout_info, &dtls_keys))
            || (result = def->prepare_connection(anjay, connection,
                                                 &socket_config, inout_info)));
    if (result && connection->conn_socket_) {
        avs_net_socket_close(connection->conn_socket_);
    }
    return result;
}

static void ensure_socket_connected(anjay_t *anjay,
                                    anjay_connections_t *connections,
                                    anjay_connection_type_t conn_type,
                                    anjay_connection_info_t *inout_info) {
    anjay_server_connection_t *connection =
            _anjay_connection_get(connections, conn_type);
    assert(connection);
    const anjay_connection_type_definition_t *def =
            get_connection_type_def(conn_type);
    assert(def);
    avs_net_abstract_socket_t *existing_socket =
            _anjay_connection_internal_get_socket(connection);

    if (existing_socket == NULL
            && recreate_socket(anjay, def, connection, inout_info)) {
        connection->state = ANJAY_SERVER_CONNECTION_ERROR;
        on_connection_refreshed(anjay, connections);
        return;
    }
    _anjay_connection_internal_bring_online(anjay, connections, conn_type);
}

static void refresh_connection(anjay_t *anjay,
                               anjay_connections_t *connections,
                               anjay_connection_type_t conn_type,
                               anjay_connection_info_t *inout_info) {
    anjay_server_connection_t *out_connection =
            _anjay_connection_get(connections, conn_type);
    assert(out_connection);

    _anjay_url_cleanup(&out_connection->uri);
    out_connection->mode =
            _anjay_get_connection_mode(inout_info->binding_mode, conn_type);
    if (out_connection->mode == ANJAY_CONNECTION_DISABLED) {
        _anjay_connection_internal_clean_socket(anjay, out_connection);
        out_connection->state = ANJAY_SERVER_CONNECTION_STABLE;
        out_connection->needs_observe_flush = false;
        on_connection_refreshed(anjay, connections);
    } else {
        ensure_socket_connected(anjay, connections, conn_type, inout_info);
    }
}

void _anjay_connections_refresh(anjay_t *anjay,
                                anjay_connections_t *connections,
                                anjay_iid_t security_iid,
                                const anjay_url_t *uri,
                                const char *binding_mode) {
    anjay_connection_info_t server_info = {
        .security_iid = security_iid,
        .uri = uri,
        .binding_mode = binding_mode
    };

    connections->primary_connection = ANJAY_CONNECTION_UNSET;

    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        anjay_server_connection_t *connection =
                _anjay_connection_get(connections, conn_type);
        connection->state = ANJAY_SERVER_CONNECTION_IN_PROGRESS;
        _anjay_sched_del(anjay->sched,
                         &connection->queue_mode_close_socket_clb);
    }
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        refresh_connection(anjay, connections, conn_type, &server_info);
    }
}

int _anjay_get_security_info(anjay_t *anjay,
                             avs_net_security_info_t *out_net_info,
                             anjay_server_dtls_keys_t *out_dtls_keys,
                             anjay_iid_t security_iid,
                             anjay_connection_type_t conn_type) {
    const anjay_connection_type_definition_t *conn_def =
            get_connection_type_def(conn_type);
    anjay_connection_info_t info = {
        .security_iid = security_iid
    };

    memset(out_dtls_keys, 0, sizeof(*out_dtls_keys));
    int result;
    (void) ((result =
                     conn_def->get_connection_info(anjay, &info, out_dtls_keys))
            || (result = conn_def->get_net_security_info(out_net_info, &info,
                                                         out_dtls_keys)));
    return result;
}
