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

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_stream_inbuf.h>
#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/udp.h>

#include "../anjay_core.h"
#include "../anjay_io_core.h"
#include "../anjay_servers_utils.h"

#include "../dm/anjay_query.h"

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "anjay_activate.h"
#include "anjay_connections_internal.h"
#include "anjay_reload.h"
#include "anjay_security.h"
#include "anjay_server_connections.h"

VISIBILITY_SOURCE_BEGIN

avs_net_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection) {
    return connection->conn_socket_;
}

void _anjay_connection_internal_clean_socket(
        anjay_t *anjay, anjay_server_connection_t *connection) {
    _anjay_coap_ctx_cleanup(anjay, &connection->coap_ctx);
    _anjay_socket_cleanup(anjay, &connection->conn_socket_);
    avs_sched_del(&connection->queue_mode_close_socket_clb);
}

avs_error_t
_anjay_connection_init_psk_security(anjay_t *anjay,
                                    anjay_iid_t security_iid,
                                    anjay_rid_t identity_rid,
                                    anjay_rid_t secret_key_rid,
                                    avs_net_security_info_t *security,
                                    void **out_psk_buffer) {
    assert(anjay);
    assert(out_psk_buffer && !*out_psk_buffer);
    avs_stream_t *membuf = avs_stream_membuf_create();
    if (!membuf) {
        anjay_log(ERROR, _("out of memory"));
        return avs_errno(AVS_ENOMEM);
    }

    avs_error_t err = AVS_OK;
    avs_off_t psk_offset;
    anjay_uri_path_t path;
    if (((path = MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                                    identity_rid)),
         _anjay_dm_read_resource_into_stream(anjay, &path, membuf))
            || avs_is_err((err = avs_stream_offset(membuf, &psk_offset)))
            || psk_offset < 0
            || ((path = MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                                           secret_key_rid)),
                _anjay_dm_read_resource_into_stream(anjay, &path, membuf))) {
        anjay_log(WARNING, _("read ") "%s" _(" failed"),
                  ANJAY_DEBUG_MAKE_PATH(&path));
        if (avs_is_ok(err)) {
            err = avs_errno(AVS_EPROTO);
        }
    }

    size_t buffer_size;
    if (avs_is_ok(err)
            && avs_is_ok((err = avs_stream_membuf_take_ownership(
                                  membuf, out_psk_buffer, &buffer_size)))) {
        *security = avs_net_security_info_from_psk((avs_net_psk_info_t) {
            .identity = *out_psk_buffer,
            .identity_size = (size_t) psk_offset,
            .psk = (char *) *out_psk_buffer + psk_offset,
            .psk_size = buffer_size - (size_t) psk_offset
        });
    }

    avs_stream_cleanup(&membuf);
    return err;
}

static const anjay_connection_type_definition_t *
get_connection_type_def(anjay_socket_transport_t type) {
    switch (type) {
#ifdef WITH_AVS_COAP_UDP
    case ANJAY_SOCKET_TRANSPORT_UDP:
        return &ANJAY_CONNECTION_DEF_UDP;
#endif // WITH_AVS_COAP_UDP
    default:
        return NULL;
    }
}

avs_error_t _anjay_server_connection_internal_bring_online(
        anjay_server_info_t *server,
        anjay_connection_type_t conn_type,
        const anjay_iid_t *security_iid) {
    assert(server);
    anjay_server_connection_t *connection =
            _anjay_connection_get(&server->connections, conn_type);
    assert(connection);
    assert(connection->conn_socket_);
    assert(!connection->queue_mode_close_socket_clb);

    const anjay_connection_type_definition_t *def =
            get_connection_type_def(connection->transport);
    assert(def);

    (void) security_iid;

    if (_anjay_connection_is_online(connection)) {
        anjay_log(DEBUG, _("socket already connected"));
        connection->state = ANJAY_SERVER_CONNECTION_STABLE;
        connection->needs_observe_flush = true;
        return AVS_OK;
    }

    avs_error_t err = avs_errno(AVS_ENOMEM);
    if (def->ensure_coap_context(server->anjay, connection)
            || avs_is_err((
                       err = def->connect_socket(server->anjay, connection)))) {
        connection->state = ANJAY_SERVER_CONNECTION_OFFLINE;
        _anjay_coap_ctx_cleanup(server->anjay, &connection->coap_ctx);

        if (avs_is_err(avs_net_socket_close(connection->conn_socket_))) {
            anjay_log(ERROR, _("Could not close the socket (?!)"));
        }
        return err;
    }

    const bool session_resumed =
            _anjay_was_session_resumed(connection->conn_socket_);
    if (!session_resumed) {
        _anjay_conn_session_token_reset(&connection->session_token);
    }
    anjay_log(INFO, session_resumed ? "resumed connection" : "reconnected");
    connection->state = ANJAY_SERVER_CONNECTION_FRESHLY_CONNECTED;
    connection->needs_observe_flush = true;
    return AVS_OK;
}

static void connection_cleanup(anjay_t *anjay,
                               anjay_server_connection_t *connection) {
    _anjay_connection_internal_clean_socket(anjay, connection);
    _anjay_url_cleanup(&connection->uri);
}

void _anjay_connections_close(anjay_t *anjay,
                              anjay_connections_t *connections) {
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        connection_cleanup(anjay,
                           _anjay_connection_get(connections, conn_type));
    }
}

anjay_conn_session_token_t
_anjay_connections_get_primary_session_token(anjay_connections_t *connections) {
    return _anjay_connection_get(connections, ANJAY_CONNECTION_PRIMARY)
            ->session_token;
}

void _anjay_connection_internal_invalidate_session(
        anjay_server_connection_t *connection) {
    memset(connection->nontransient_state.dtls_session_buffer, 0,
           sizeof(connection->nontransient_state.dtls_session_buffer));
}

static avs_error_t
recreate_socket(anjay_t *anjay,
                const anjay_connection_type_definition_t *def,
                anjay_server_connection_t *connection,
                anjay_connection_info_t *inout_info) {
    avs_net_ssl_configuration_t socket_config;
    memset(&socket_config, 0, sizeof(socket_config));

    assert(!_anjay_connection_internal_get_socket(connection));
    socket_config.backend_configuration = anjay->socket_config;
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
    socket_config.server_name_indication = inout_info->sni.sni;
    socket_config.use_connection_id = anjay->use_connection_id;
    socket_config.prng_ctx = anjay->prng_ctx.ctx;

    // At this point, inout_info has "global" settings filled,
    // but transport-specific (i.e. UDP or SMS) fields are not
    anjay_security_config_t security_config;
    anjay_security_config_cache_t security_config_cache;
    memset(&security_config_cache, 0, sizeof(security_config_cache));
    avs_error_t err;
    {
        err = _anjay_connection_security_generic_get_config(
                anjay, &security_config, &security_config_cache, inout_info);
    }
    if (avs_is_err(err)) {
        anjay_log(DEBUG,
                  _("could not get ") "%s" _(
                          " security config for server ") "/%u/%u",
                  def->name, ANJAY_DM_OID_SECURITY, inout_info->security_iid);
    } else {
        socket_config.security = security_config.security_info;
        socket_config.ciphersuites = security_config.tls_ciphersuites;
        if (avs_is_err((err = def->prepare_connection(
                                anjay, connection, &socket_config,
                                security_config.dane_tlsa_record, inout_info)))
                && connection->conn_socket_) {
            avs_net_socket_shutdown(connection->conn_socket_);
            avs_net_socket_close(connection->conn_socket_);
        }
    }
    _anjay_security_config_cache_cleanup(&security_config_cache);
    return err;
}

static avs_error_t
ensure_socket_connected(anjay_server_info_t *server,
                        anjay_connection_type_t conn_type,
                        anjay_connection_info_t *inout_info) {
    anjay_server_connection_t *connection =
            _anjay_connection_get(&server->connections, conn_type);
    assert(connection);
    const anjay_connection_type_definition_t *def =
            get_connection_type_def(connection->transport);
    assert(def);
    avs_net_socket_t *existing_socket =
            _anjay_connection_internal_get_socket(connection);

    if (existing_socket == NULL) {
        avs_error_t err =
                recreate_socket(server->anjay, def, connection, inout_info);
        if (avs_is_err(err)) {
            connection->state = ANJAY_SERVER_CONNECTION_OFFLINE;
            return err;
        }
    }

    return _anjay_server_connection_internal_bring_online(
            server, conn_type, &inout_info->security_iid);
}

static avs_error_t refresh_connection(anjay_server_info_t *server,
                                      anjay_connection_type_t conn_type,
                                      bool enabled,
                                      anjay_connection_info_t *inout_info) {
    anjay_server_connection_t *out_connection =
            _anjay_connection_get(&server->connections, conn_type);
    assert(out_connection);

    _anjay_url_cleanup(&out_connection->uri);

    if (!enabled) {
        if (conn_type == ANJAY_CONNECTION_PRIMARY) {
            _anjay_connection_suspend((anjay_connection_ref_t) {
                .server = server,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
            out_connection->state = ANJAY_SERVER_CONNECTION_OFFLINE;
        } else {
            // Disabled trigger connection does not matter much,
            // so treat it as stable
            _anjay_connection_internal_clean_socket(server->anjay,
                                                    out_connection);
            out_connection->state = ANJAY_SERVER_CONNECTION_STABLE;
        }
        out_connection->needs_observe_flush = false;
        return AVS_OK;
    } else {
        return ensure_socket_connected(server, conn_type, inout_info);
    }
}

void _anjay_server_connections_refresh(
        anjay_server_info_t *server,
        anjay_iid_t security_iid,
        avs_url_t **move_uri,
        bool trigger_requested,
        const anjay_server_name_indication_t *sni) {
    anjay_connection_info_t server_info = {
        .ssid = server->ssid,
        .security_iid = security_iid,
    };
    if (*move_uri) {
        server_info.uri = *move_uri;
        server_info.transport_info = _anjay_transport_info_by_uri_scheme(
                avs_url_protocol(*move_uri));
        *move_uri = NULL;
    }
    memcpy(&server_info.sni, sni, sizeof(*sni));

    if (security_iid != ANJAY_ID_INVALID) {
        server->last_used_security_iid = security_iid;
    }
    anjay_server_connection_t *primary_conn =
            _anjay_connection_get(&server->connections,
                                  ANJAY_CONNECTION_PRIMARY);
    if (server_info.transport_info
            && (!_anjay_socket_transport_supported(
                        server->anjay, server_info.transport_info->transport)
                || !_anjay_socket_transport_is_online(
                           server->anjay,
                           server_info.transport_info->transport))) {
        anjay_log(WARNING,
                  _("transport required for protocol ") "%s" _(
                          " is not supported or offline"),
                  server_info.transport_info->uri_scheme);
        server_info.transport_info = NULL;
    }
    if (server_info.transport_info
            && primary_conn->transport
                           != server_info.transport_info->transport) {
        const char *host = avs_url_host(server_info.uri);
        const char *port = avs_url_port(server_info.uri);
        anjay_log(INFO,
                  _("server /0/") "%u" _(": transport change ") "%c" _(
                          " -> ") "%c" _(" (uri: ") "%s" _(":") "%s" _(")"),
                  security_iid,
                  _anjay_binding_info_by_transport(primary_conn->transport)
                          ->letter,
                  _anjay_binding_info_by_transport(
                          server_info.transport_info->transport)
                          ->letter,
                  host ? host : "", port ? port : "");
        // change in transport binding requries creating a different type of
        // socket and possibly CoAP context
        connection_cleanup(server->anjay, primary_conn);
        primary_conn->transport = server_info.transport_info->transport;
        server->registration_info.expire_time = AVS_TIME_REAL_INVALID;
    }

    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        anjay_server_connection_t *connection =
                _anjay_connection_get(&server->connections, conn_type);
        connection->state = ANJAY_SERVER_CONNECTION_IN_PROGRESS;
        avs_sched_del(&connection->queue_mode_close_socket_clb);
    }
    avs_error_t err =
            refresh_connection(server, ANJAY_CONNECTION_PRIMARY,
                               !!server_info.transport_info, &server_info);
    (void) trigger_requested;

    // TODO T2391: fall back to another transport if connection failed
    _anjay_server_on_refreshed(server, primary_conn->state, err);
    _anjay_connection_info_cleanup(&server_info);
}

avs_error_t _anjay_get_security_config(anjay_t *anjay,
                                       anjay_security_config_t *out_config,
                                       anjay_security_config_cache_t *cache,
                                       anjay_ssid_t ssid,
                                       anjay_iid_t security_iid) {
    anjay_connection_info_t info = {
        .ssid = ssid,
        .security_iid = security_iid
    };
    avs_error_t err =
            _anjay_connection_security_generic_get_config(anjay, out_config,
                                                          cache, &info);
    _anjay_connection_info_cleanup(&info);
    return err;
}

bool _anjay_socket_transport_supported(anjay_t *anjay,
                                       anjay_socket_transport_t type) {
    if (get_connection_type_def(type) == NULL) {
        return false;
    }
    (void) anjay;
    return true;
}
