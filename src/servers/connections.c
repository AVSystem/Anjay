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

#include "../servers_utils.h"

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "connections_internal.h"
#include "reload.h"

VISIBILITY_SOURCE_BEGIN

avs_net_abstract_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection) {
    return connection->conn_socket_;
}

void _anjay_connection_internal_clean_socket(
        anjay_server_connection_t *connection) {
    avs_net_socket_cleanup(&connection->conn_socket_);
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

int _anjay_connection_internal_bring_online(
        anjay_t *anjay, anjay_server_connection_t *connection) {
    assert(connection);
    assert(connection->conn_socket_);
    assert(!_anjay_connection_is_online(connection));

    avs_net_socket_opt_value_t session_resumed;
    char remote_hostname[ANJAY_MAX_URL_HOSTNAME_SIZE];
    char remote_port[ANJAY_MAX_URL_PORT_SIZE];
    if (avs_net_socket_get_remote_hostname(connection->conn_socket_,
                                           remote_hostname,
                                           sizeof(remote_hostname))
            || avs_net_socket_get_remote_port(connection->conn_socket_,
                                              remote_port,
                                              sizeof(remote_port))) {
        anjay_log(ERROR, "Could not get peer address and port "
                         "of a suspended connection");
        return -1;
    }

    anjay_socket_bind_config_t bind_config = {
        .family = _anjay_socket_af_from_preferred_endpoint(
                &connection->nontransient_state.preferred_endpoint),
        .last_local_port_buffer =
                &connection->nontransient_state.last_local_port,
        .static_port_preference = anjay->udp_listen_port
    };

    if (_anjay_bind_and_connect_socket(connection->conn_socket_, &bind_config,
                                       remote_hostname, remote_port)) {
        goto close_and_fail;
    }

    session_resumed.flag = true;
    if (avs_net_socket_get_opt(connection->conn_socket_,
                               AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                               &session_resumed)) {
        // if avs_net_socket_get_opt() failed, it means that it's not a DTLS
        // socket; if remote_port is empty, it means that it's an SMS socket;
        // we treat a non-DTLS SMS socket as always "resumed",
        // because MSISDN will not change during the library lifetime
        session_resumed.flag = !*remote_port;
    }
    if (!session_resumed.flag) {
        _anjay_conn_session_token_reset(&connection->session_token);
    }
    anjay_log(INFO, "%s to %s:%s",
              session_resumed.flag ? "resumed connection" : "reconnected",
              remote_hostname, remote_port);
    return 0;

    // Labels must be followed by a statement, not a declaration.
    // Fortunately, `;` is a perfectly fine statement in C.
close_and_fail:;
    int result = avs_net_socket_errno(connection->conn_socket_);
    if (avs_net_socket_close(connection->conn_socket_)) {
        anjay_log(ERROR, "Could not close the socket (?!)");
    }
    return result;
}

avs_net_af_t _anjay_socket_af_from_preferred_endpoint(
        const avs_net_resolved_endpoint_t *endpoint) {
    /*
     * The first time we connect to the server, there is no "preferred
     * endpoint" set yet, so endpoint is left uninitialized (filled with
     * zeros).
     */
    if (endpoint->size == 0) {
        return AVS_NET_AF_UNSPEC;
    }

    /*
     * Whenever the socket is bound by connect(), the address family is set to
     * match the remote address. If the socket is bound by a bind() call with
     * NULL local_addr argument, the address family falls back to the original
     * socket preference - by default, AF_UNSPEC. This causes avs_net to attempt
     * to bind to [::]:$PORT, even though the remote host may be an IPv4
     * address. This generally works, because IPv4-mapped IPv6 addresses are a
     * thing.
     *
     * On FreeBSD though, IPv4-mapped IPv6 are disabled by default (see:
     * "Interaction between IPv4/v6 sockets" at
     * https://www.freebsd.org/cgi/man.cgi?query=inet6&sektion=4), which
     * effectively breaks all connect() calls after re-binding to a recently
     * used port.
     *
     * To avoid that, we need to provide a local wildcard address appropriate
     * for the family used by the remote host. This function determines which
     * address family to use; it is then converted into a local address by
     * bind_socket() in utils_core.c.
     */
    char remote_preferred_host[sizeof(
            "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")];
    if (!avs_net_resolved_endpoint_get_host(endpoint, remote_preferred_host,
                                            sizeof(remote_preferred_host))) {
        if (strchr(remote_preferred_host, ':') != NULL) {
            return AVS_NET_AF_INET6;
        } else if (strchr(remote_preferred_host, '.') != NULL) {
            return AVS_NET_AF_INET4;
        }
    }
    return AVS_NET_AF_UNSPEC;
}

static void connection_cleanup(const anjay_t *anjay,
                               anjay_server_connection_t *connection) {
    _anjay_connection_internal_clean_socket(connection);
    _anjay_sched_del(anjay->sched,
                     &connection->queue_mode_close_socket_clb_handle);
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
    if (connections->primary_conn_type == ANJAY_CONNECTION_UNSET) {
        anjay_connection_type_t conn_type;
        ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
            if (_anjay_connection_is_online(
                        _anjay_connection_get(connections, conn_type))) {
                connections->primary_conn_type = conn_type;
                break;
            }
        }
    }
    return connections->primary_conn_type;
}

anjay_conn_session_token_t
_anjay_connections_get_primary_session_token(anjay_connections_t *connections) {
    if (connections->primary_conn_type == ANJAY_CONNECTION_UNSET) {
        anjay_conn_session_token_t result;
        _anjay_conn_session_token_reset(&result);
        return result;
    } else {
        return _anjay_connection_get(connections,
                                     connections->primary_conn_type)
                ->session_token;
    }
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
    _anjay_connection_internal_clean_socket(connection);

    // Socket configuration is slightly different between UDP and SMS
    // connections. That's why we do the common configuration here...
    avs_net_ssl_configuration_t socket_config;
    memset(&socket_config, 0, sizeof(socket_config));
    socket_config.version = anjay->dtls_version;
    socket_config.session_resumption_buffer =
            connection->nontransient_state.dtls_session_buffer;
    socket_config.session_resumption_buffer_size =
            sizeof(connection->nontransient_state.dtls_session_buffer);
    int result = def->get_net_security_info(&socket_config.security, inout_info,
                                            &dtls_keys);
    // ...and pass it as an in/out argument to create_connected_socket() so that
    // it can do any protocol-specific modifications.
    if (!result) {
        result = def->create_connected_socket(anjay, connection, &socket_config,
                                              inout_info);
    }
    if (!result) {
        avs_net_socket_opt_value_t session_resumed;
        if (avs_net_socket_get_opt(connection->conn_socket_,
                                   AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                   &session_resumed)
                || !session_resumed.flag) {
            _anjay_conn_session_token_reset(&connection->session_token);
        }
    } else {
        avs_net_abstract_socket_t *sock = connection->conn_socket_;
        if (sock) {
            avs_net_socket_close(sock);
        }
    }
    return result;
}

static int
ensure_socket_connected(anjay_t *anjay,
                        const anjay_connection_type_definition_t *def,
                        anjay_server_connection_t *connection,
                        anjay_connection_info_t *inout_info) {
    avs_net_abstract_socket_t *existing_socket =
            _anjay_connection_internal_get_socket(connection);

    if (existing_socket == NULL) {
        return recreate_socket(anjay, def, connection, inout_info);
    } else if (!_anjay_connection_is_online(connection)) {
        return _anjay_connection_internal_bring_online(anjay, connection);
    }
    return 0;
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

static int refresh_connection(anjay_t *anjay,
                              anjay_connections_t *connections,
                              anjay_connection_type_t conn_type,
                              anjay_connection_info_t *inout_info) {
    anjay_server_connection_t *out_connection =
            _anjay_connection_get(connections, conn_type);
    assert(out_connection);
    const anjay_connection_type_definition_t *def =
            get_connection_type_def(conn_type);
    assert(def);

    out_connection->mode =
            _anjay_get_connection_mode(inout_info->binding_mode, conn_type);
    if (out_connection->mode == ANJAY_CONNECTION_DISABLED) {
        _anjay_connection_internal_clean_socket(out_connection);
        _anjay_conn_session_token_reset(&out_connection->session_token);
        return 0;
    } else {
        return ensure_socket_connected(anjay, def, out_connection, inout_info);
    }
}

static bool is_connected(anjay_connections_t *connections) {
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        if (_anjay_connection_is_online(
                    _anjay_connection_get(connections, conn_type))) {
            return true;
        }
    }
    return false;
}

int _anjay_connections_refresh(anjay_t *anjay,
                               anjay_connections_t *connections,
                               anjay_iid_t security_iid,
                               const anjay_url_t *uri,
                               const char *binding_mode) {
    anjay_connection_info_t server_info = {
        .security_iid = security_iid,
        .uri = uri,
        .binding_mode = binding_mode
    };

    anjay_conn_session_token_t previous_session_token =
            _anjay_connections_get_primary_session_token(connections);

    int results[ANJAY_CONNECTION_LIMIT_];
    int first_error = 0;
    for (size_t i = 0; i < AVS_ARRAY_SIZE(results); ++i) {
        if ((results[i] = refresh_connection(anjay, connections,
                                             (anjay_connection_type_t) i,
                                             &server_info))
                && !first_error) {
            first_error = results[i];
        }
    }

    if (!is_connected(connections)) {
        return first_error ? first_error : -1;
    }
    if (first_error) {
        // some connection is available, but some other failed
        _anjay_schedule_delayed_reload_servers(anjay);
    }

    if (!_anjay_conn_session_tokens_equal(
                previous_session_token,
                _anjay_connections_get_primary_session_token(connections))) {
        // The following causes _anjay_connections_get_primary() to search for
        // the primary connection again. In conjunction with what is done in
        // _anjay_active_server_refresh(), this forces functions such as
        // send_update_sched_job() or reload_active_server() to schedule
        // reactivation of the server, and forces
        // _anjay_server_ensure_valid_registration() to send Register.
        connections->primary_conn_type = ANJAY_CONNECTION_UNSET;
    }

    anjay_connection_type_t primary_conn_type =
            _anjay_connections_get_primary(connections);
    return primary_conn_type != ANJAY_CONNECTION_UNSET
                   ? results[primary_conn_type]
                   : first_error;
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
