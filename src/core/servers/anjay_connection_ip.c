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

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/udp.h>

#include <inttypes.h>

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "anjay_connections_internal.h"
#include "anjay_servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static const avs_net_dtls_handshake_timeouts_t *
get_tls_handshake_timeouts(anjay_t *anjay) {
    return &anjay->udp_dtls_hs_tx_params;
}

static avs_error_t
prepare_connection(anjay_t *anjay,
                   anjay_server_connection_t *out_conn,
                   const avs_net_ssl_configuration_t *socket_config,
                   const avs_net_socket_dane_tlsa_record_t *dane_tlsa_record,
                   const anjay_connection_info_t *info) {
    (void) anjay;

    const char *uri_scheme = avs_url_protocol(info->uri);
    if (!info->transport_info || !info->transport_info->socket_type) {
        anjay_log(ERROR,
                  _("Protocol ") "%s" _(" is not supported for IP transports"),
                  uri_scheme ? uri_scheme : "(unknown)");
        return avs_errno(AVS_EINVAL);
    }

    if (_anjay_url_from_avs_url(info->uri, &out_conn->uri)) {
        return avs_errno(AVS_ENOMEM);
    }

    out_conn->stateful = true;
    avs_net_socket_t *socket = NULL;
    bool is_tls = false;
    switch (*info->transport_info->socket_type) {
    case AVS_NET_TCP_SOCKET:
        avs_net_tcp_socket_create(&socket,
                                  &socket_config->backend_configuration);
        break;
    case AVS_NET_UDP_SOCKET:
        avs_net_udp_socket_create(&socket,
                                  &socket_config->backend_configuration);
        out_conn->stateful = false;
        break;
    case AVS_NET_SSL_SOCKET:
        is_tls = true;
        avs_net_ssl_socket_create(&socket, socket_config);
        break;
    case AVS_NET_DTLS_SOCKET:
        is_tls = true;
        avs_net_dtls_socket_create(&socket, socket_config);
        break;
    default:
        break;
    }
    if (!socket) {
        anjay_log(ERROR, _("could not create CoAP socket"));
        return avs_errno(AVS_ENOMEM);
    }

    avs_error_t err = AVS_OK;
    if (is_tls && dane_tlsa_record
            && avs_is_err((err = avs_net_socket_set_opt(
                                   socket, AVS_NET_SOCKET_OPT_DANE_TLSA_ARRAY,
                                   (avs_net_socket_opt_value_t) {
                                       .dane_tlsa_array = {
                                           .array_ptr = dane_tlsa_record,
                                           .array_element_count = 1
                                       }
                                   })))) {
        _anjay_socket_cleanup(anjay, &socket);
        anjay_log(ERROR, _("could not configure DANE TLSA record: ") "%s",
                  AVS_COAP_STRERROR(err));
        return err;
    }

    out_conn->conn_socket_ = socket;
    return AVS_OK;
}

static avs_error_t connect_socket(anjay_t *anjay,
                                  anjay_server_connection_t *connection) {
    (void) anjay;

    avs_net_socket_t *socket =
            _anjay_connection_internal_get_socket(connection);
    avs_error_t err = avs_net_socket_connect(socket, connection->uri.host,
                                             connection->uri.port);
    if (avs_is_err(err)) {
        anjay_log(ERROR, _("could not connect to ") "%s" _(":") "%s",
                  connection->uri.host, connection->uri.port);
        return err;
    }

    if (!avs_coap_ctx_has_socket(connection->coap_ctx)
            && avs_is_err((err = avs_coap_ctx_set_socket(connection->coap_ctx,
                                                         socket)))) {
        anjay_log(ERROR, _("could not assign socket to CoAP/UDP context"));
        return err;
    }

    if (avs_is_ok(avs_net_socket_get_local_port(
                socket, connection->nontransient_state.last_local_port,
                ANJAY_MAX_URL_PORT_SIZE))) {
        anjay_log(DEBUG, _("bound to port ") "%s",
                  connection->nontransient_state.last_local_port);
    } else {
        anjay_log(WARNING, _("could not store bound local port"));
        connection->nontransient_state.last_local_port[0] = '\0';
    }

    return AVS_OK;
}

#ifdef WITH_AVS_COAP_UDP
static int ensure_udp_coap_context(anjay_t *anjay,
                                   anjay_server_connection_t *connection) {
    if (!connection->coap_ctx) {
        connection->coap_ctx = avs_coap_udp_ctx_create(
                anjay->sched, &anjay->udp_tx_params, anjay->in_shared_buffer,
                anjay->out_shared_buffer, anjay->udp_response_cache,
                anjay->prng_ctx.ctx);
        if (!connection->coap_ctx) {
            anjay_log(ERROR, _("could not create CoAP/UDP context"));
            return -1;
        }
    }
    return 0;
}

static avs_error_t
try_bind_to_static_preferred_port(anjay_t *anjay,
                                  anjay_server_connection_t *connection) {
    if (anjay->udp_listen_port) {
        char static_preferred_port[ANJAY_MAX_URL_PORT_SIZE] = "";
        if (anjay->udp_listen_port
                && avs_simple_snprintf(static_preferred_port,
                                       sizeof(static_preferred_port),
                                       "%" PRIu16, anjay->udp_listen_port)
                               < 0) {
            AVS_UNREACHABLE("Could not convert preferred port number");
        }
        avs_error_t err = avs_net_socket_bind(
                _anjay_connection_internal_get_socket(connection), NULL,
                static_preferred_port);
        if (avs_is_err(err)) {
            anjay_log(ERROR, _("could not bind socket to port ") "%s",
                      static_preferred_port);
            return err;
        }
    }
    return AVS_OK;
}

static avs_error_t
try_bind_to_last_local_port(anjay_server_connection_t *connection,
                            const char *local_addr) {
    avs_error_t err = avs_errno(AVS_EBADF);
    if (*connection->nontransient_state.last_local_port) {
        if (avs_is_ok((
                    err = avs_net_socket_bind(
                            _anjay_connection_internal_get_socket(connection),
                            local_addr,
                            connection->nontransient_state.last_local_port)))) {
            return AVS_OK;
        }
        anjay_log(WARNING,
                  _("could not bind socket to last known address ") "[%s]:%s",
                  local_addr ? local_addr : "",
                  connection->nontransient_state.last_local_port);
    }
    return err;
}

static const char *
get_preferred_local_addr(const anjay_server_connection_t *connection) {
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
     * for the family used by the remote host. However, the first time we
     * connect to the server, there is no "preferred endpoint" set yet, so
     * endpoint is left uninitialized (filled with zeros) - that's why we check
     * the size first.
     */
    char remote_preferred_host[sizeof(
            "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")];
    if (connection->nontransient_state.preferred_endpoint.size > 0
            && avs_is_ok(avs_net_resolved_endpoint_get_host(
                       &connection->nontransient_state.preferred_endpoint,
                       remote_preferred_host, sizeof(remote_preferred_host)))) {
        if (strchr(remote_preferred_host, ':') != NULL) {
            return "::";
        } else if (strchr(remote_preferred_host, '.') != NULL) {
            return "0.0.0.0";
        }
    }
    return NULL;
}

static avs_error_t connect_udp_socket(anjay_t *anjay,
                                      anjay_server_connection_t *connection) {
    const char *local_addr = get_preferred_local_addr(connection);
    avs_error_t err;
    if (avs_is_err(try_bind_to_last_local_port(connection, local_addr))
            && avs_is_err((err = try_bind_to_static_preferred_port(
                                   anjay, connection)))) {
        return err;
    }

    return connect_socket(anjay, connection);
}

const anjay_connection_type_definition_t ANJAY_CONNECTION_DEF_UDP = {
    .name = "UDP",
    .get_dtls_handshake_timeouts = get_tls_handshake_timeouts,
    .prepare_connection = prepare_connection,
    .ensure_coap_context = ensure_udp_coap_context,
    .connect_socket = connect_udp_socket
};
#endif // WITH_AVS_COAP_UDP
