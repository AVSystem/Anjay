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

#include <avsystem/commons/errno.h>
#include <avsystem/commons/utils.h>

#include <inttypes.h>

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "connections_internal.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static int get_udp_security_mode(anjay_t *anjay,
                                 anjay_iid_t security_iid,
                                 anjay_udp_security_mode_t *out_mode) {
    int64_t mode;
    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_MODE);

    if (_anjay_dm_res_read_i64(anjay, &path, &mode)) {
        anjay_log(ERROR, "could not read LwM2M server security mode");
        return -1;
    }

    switch (mode) {
    case ANJAY_UDP_SECURITY_RPK:
        anjay_log(ERROR, "unsupported security mode: %" PRId64, mode);
        return -1;
    case ANJAY_UDP_SECURITY_NOSEC:
    case ANJAY_UDP_SECURITY_PSK:
    case ANJAY_UDP_SECURITY_CERTIFICATE:
        *out_mode = (anjay_udp_security_mode_t) mode;
        return 0;
    default:
        anjay_log(ERROR, "invalid security mode: %" PRId64, mode);
        return -1;
    }
}

static const char *uri_protocol_as_string(anjay_url_protocol_t protocol) {
    switch (protocol) {
    case ANJAY_URL_PROTOCOL_COAP:
        return "coap";
    case ANJAY_URL_PROTOCOL_COAPS:
        return "coaps";
    }
    AVS_UNREACHABLE("invalid protocol in anjay_uri_t");
    return "(invalid)";
}

static bool uri_protocol_matching(anjay_udp_security_mode_t security_mode,
                                  const anjay_url_t *uri) {
    anjay_url_protocol_t expected_proto =
            (security_mode == ANJAY_UDP_SECURITY_NOSEC)
                    ? ANJAY_URL_PROTOCOL_COAP
                    : ANJAY_URL_PROTOCOL_COAPS;

    if (uri->protocol != expected_proto) {
        anjay_log(WARNING,
                  "URI protocol mismatch: security mode %d requires "
                  "'%s', but '%s' was configured",
                  (int) security_mode, uri_protocol_as_string(expected_proto),
                  uri_protocol_as_string(uri->protocol));
        return false;
    }

    return true;
}

static int get_udp_dtls_keys(anjay_t *anjay,
                             anjay_iid_t security_iid,
                             anjay_udp_security_mode_t security_mode,
                             anjay_server_dtls_keys_t *out_keys) {
    if (security_mode == ANJAY_UDP_SECURITY_NOSEC) {
        return 0;
    }

    const struct {
        bool required;
        anjay_rid_t rid;
        char *buffer;
        size_t buffer_capacity;
        size_t *buffer_size_ptr;
    } values[] = {
        {
            .required = true,
            .rid = ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY,
            .buffer = out_keys->pk_or_identity,
            .buffer_capacity = sizeof(out_keys->pk_or_identity),
            .buffer_size_ptr = &out_keys->pk_or_identity_size
        },
        {
            .required = security_mode != ANJAY_UDP_SECURITY_PSK,
            .rid = ANJAY_DM_RID_SECURITY_SERVER_PK_OR_IDENTITY,
            .buffer = out_keys->server_pk_or_identity,
            .buffer_capacity = sizeof(out_keys->server_pk_or_identity),
            .buffer_size_ptr = &out_keys->server_pk_or_identity_size
        },
        {
            .required = true,
            .rid = ANJAY_DM_RID_SECURITY_SECRET_KEY,
            .buffer = out_keys->secret_key,
            .buffer_capacity = sizeof(out_keys->secret_key),
            .buffer_size_ptr = &out_keys->secret_key_size
        }
    };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(values); ++i) {
        const anjay_uri_path_t path =
                MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                                   values[i].rid);
        if (_anjay_dm_res_read(anjay, &path, values[i].buffer,
                               values[i].buffer_capacity,
                               values[i].buffer_size_ptr)
                && values[i].required) {
            anjay_log(WARNING, "read %s failed", ANJAY_DEBUG_MAKE_PATH(&path));
            return -1;
        }
    }

    return 0;
}

static const avs_net_dtls_handshake_timeouts_t *
get_udp_dtls_handshake_timeouts(anjay_t *anjay) {
    return &anjay->udp_dtls_hs_tx_params;
}

static int get_udp_connection_info(anjay_t *anjay,
                                   anjay_connection_info_t *inout_info,
                                   anjay_server_dtls_keys_t *dtls_keys) {
    if (get_udp_security_mode(anjay, inout_info->security_iid,
                              &inout_info->udp.security_mode)
            || (inout_info->uri
                && !uri_protocol_matching(inout_info->udp.security_mode,
                                          inout_info->uri))
            || get_udp_dtls_keys(anjay, inout_info->security_iid,
                                 inout_info->udp.security_mode, dtls_keys)) {
        return -1;
    }

    anjay_log(DEBUG, "server /%u/%u: UDP security mode = %d",
              ANJAY_DM_OID_SECURITY, inout_info->security_iid,
              (int) inout_info->udp.security_mode);
    return 0;
}

static int init_cert_security(avs_net_security_info_t *security,
                              const anjay_server_dtls_keys_t *keys) {
    avs_net_client_cert_info_t client_cert =
            avs_net_client_cert_info_from_buffer(keys->pk_or_identity,
                                                 keys->pk_or_identity_size);

    avs_net_client_key_info_t private_key =
            avs_net_client_key_info_from_buffer(keys->secret_key,
                                                keys->secret_key_size, NULL);

    const void *raw_cert_der = keys->server_pk_or_identity_size > 0
                                       ? keys->server_pk_or_identity
                                       : NULL;
    avs_net_trusted_cert_info_t ca = avs_net_trusted_cert_info_from_buffer(
            raw_cert_der, keys->server_pk_or_identity_size);

    *security = avs_net_security_info_from_certificates(
            (avs_net_certificate_info_t) {
                .server_cert_validation = !!raw_cert_der,
                .trusted_certs = ca,
                .client_cert = client_cert,
                .client_key = private_key
            });

    return 0;
}

static int
get_udp_net_security_info(avs_net_security_info_t *out_net_info,
                          const anjay_connection_info_t *info,
                          const anjay_server_dtls_keys_t *dtls_keys) {
    switch (info->udp.security_mode) {
    case ANJAY_UDP_SECURITY_NOSEC:
        return 0;
    case ANJAY_UDP_SECURITY_PSK:
        return _anjay_connection_init_psk_security(out_net_info, dtls_keys);
    case ANJAY_UDP_SECURITY_CERTIFICATE:
        return init_cert_security(out_net_info, dtls_keys);
    case ANJAY_UDP_SECURITY_RPK:
    default:
        anjay_log(ERROR, "unsupported security mode: %d",
                  (int) info->udp.security_mode);
        return -1;
    }
}

static int
prepare_udp_connection(anjay_t *anjay,
                       anjay_server_connection_t *out_conn,
                       const avs_net_ssl_configuration_t *socket_config,
                       const anjay_connection_info_t *info) {
    (void) anjay;
    if (_anjay_url_copy(&out_conn->uri, info->uri)) {
        return -ENOMEM;
    }

    avs_net_socket_type_t type;
    const void *config_ptr;

    if (info->udp.security_mode == ANJAY_UDP_SECURITY_NOSEC) {
        type = AVS_NET_UDP_SOCKET;
        config_ptr = &socket_config->backend_configuration;
        out_conn->stateful = false;
    } else {
        type = AVS_NET_DTLS_SOCKET;
        config_ptr = socket_config;
        out_conn->stateful = true;
    }

    avs_net_abstract_socket_t *socket = NULL;
    int result = avs_net_socket_create(&socket, type, config_ptr);
    if (!socket) {
        assert(result);
        anjay_log(ERROR, "could not create CoAP socket");
        return -ENOMEM;
    }
    (void) result;
    out_conn->conn_socket_ = socket;
    return 0;
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
            && !avs_net_resolved_endpoint_get_host(
                       &connection->nontransient_state.preferred_endpoint,
                       remote_preferred_host, sizeof(remote_preferred_host))) {
        if (strchr(remote_preferred_host, ':') != NULL) {
            return "::";
        } else if (strchr(remote_preferred_host, '.') != NULL) {
            return "0.0.0.0";
        }
    }
    return NULL;
}

static int try_bind_to_last_local_port(anjay_server_connection_t *connection,
                                       const char *local_addr) {
    if (*connection->nontransient_state.last_local_port) {
        if (!avs_net_socket_bind(
                    _anjay_connection_internal_get_socket(connection),
                    local_addr,
                    connection->nontransient_state.last_local_port)) {
            return 0;
        }
        anjay_log(WARNING,
                  "could not bind socket to last known address [%s]:%s",
                  local_addr ? local_addr : "",
                  connection->nontransient_state.last_local_port);
    }
    return -1;
}

static int
try_bind_to_static_preferred_port(anjay_t *anjay,
                                  anjay_server_connection_t *connection,
                                  const char *local_addr) {
    if (local_addr || anjay->udp_listen_port) {
        char static_preferred_port[ANJAY_MAX_URL_PORT_SIZE] = "";
        if (anjay->udp_listen_port
                && avs_simple_snprintf(static_preferred_port,
                                       sizeof(static_preferred_port),
                                       "%" PRIu16, anjay->udp_listen_port)
                               < 0) {
            AVS_UNREACHABLE("Could not convert preferred port number");
        }
        if (avs_net_socket_bind(_anjay_connection_internal_get_socket(
                                        connection),
                                local_addr, static_preferred_port)) {
            anjay_log(ERROR, "could not bind socket to [%s]:%s",
                      local_addr ? local_addr : "", static_preferred_port);
            return -1;
        }
    }
    return 0;
}

static int connect_udp_socket(anjay_t *anjay,
                              anjay_server_connection_t *connection) {
    const char *local_addr = get_preferred_local_addr(connection);
    if (try_bind_to_last_local_port(connection, local_addr)
            && try_bind_to_static_preferred_port(anjay, connection,
                                                 local_addr)) {
        return -1;
    }

    avs_net_abstract_socket_t *socket =
            _anjay_connection_internal_get_socket(connection);
    if (avs_net_socket_connect(socket, connection->uri.host,
                               connection->uri.port)) {
        anjay_log(ERROR, "could not connect to %s:%s", connection->uri.host,
                  connection->uri.port);
        return -1;
    }

    if (!avs_net_socket_get_local_port(
                socket, connection->nontransient_state.last_local_port,
                ANJAY_MAX_URL_PORT_SIZE)) {
        anjay_log(DEBUG, "bound to port %s",
                  connection->nontransient_state.last_local_port);
    } else {
        anjay_log(WARNING, "could not store bound local port");
        connection->nontransient_state.last_local_port[0] = '\0';
    }

    return 0;
}

const anjay_connection_type_definition_t ANJAY_CONNECTION_DEF_UDP = {
    .name = "UDP",
    .get_dtls_handshake_timeouts = get_udp_dtls_handshake_timeouts,
    .get_connection_info = get_udp_connection_info,
    .get_net_security_info = get_udp_net_security_info,
    .prepare_connection = prepare_udp_connection,
    .connect_socket = connect_udp_socket
};
