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

#include <inttypes.h>

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "connections_internal.h"

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
                    ? ANJAY_URL_PROTOCOL_COAP : ANJAY_URL_PROTOCOL_COAPS;

    if (uri->protocol != expected_proto) {
        anjay_log(WARNING, "URI protocol mismatch: security mode %d requires "
                  "'%s', but '%s' was configured", (int) security_mode,
                  uri_protocol_as_string(expected_proto),
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
            true,
            ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY,
            out_keys->pk_or_identity,
            sizeof(out_keys->pk_or_identity),
            &out_keys->pk_or_identity_size
        }, {
            security_mode != ANJAY_UDP_SECURITY_PSK,
            ANJAY_DM_RID_SECURITY_SERVER_PK_OR_IDENTITY,
            out_keys->server_pk_or_identity,
            sizeof(out_keys->server_pk_or_identity),
            &out_keys->server_pk_or_identity_size
        }, {
            true,
            ANJAY_DM_RID_SECURITY_SECRET_KEY,
            out_keys->secret_key,
            sizeof(out_keys->secret_key),
            &out_keys->secret_key_size
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

static int
get_udp_connection_info(anjay_t *anjay,
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
    avs_net_trusted_cert_info_t ca =
            avs_net_trusted_cert_info_from_buffer(
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

static int get_udp_net_security_info(avs_net_security_info_t *out_net_info,
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
create_connected_udp_socket(anjay_t *anjay,
                            anjay_server_connection_t *out_conn,
                            avs_net_ssl_configuration_t *inout_socket_config,
                            const anjay_connection_info_t *info) {
    avs_net_socket_type_t type =
            info->udp.security_mode == ANJAY_UDP_SECURITY_NOSEC
                ? AVS_NET_UDP_SOCKET : AVS_NET_DTLS_SOCKET;

    inout_socket_config->backend_configuration = anjay->udp_socket_config;
    inout_socket_config->backend_configuration.reuse_addr = 1;
    inout_socket_config->backend_configuration.preferred_endpoint =
            &out_conn->nontransient_state.preferred_endpoint;

    const void *config_ptr = (type == AVS_NET_DTLS_SOCKET)
            ? (const void *) inout_socket_config
            : (const void *) &inout_socket_config->backend_configuration;

    avs_net_abstract_socket_t *socket = NULL;
    int result = _anjay_create_connected_udp_socket(
            &socket, type, config_ptr,
            &(const anjay_socket_bind_config_t) {
                .family = _anjay_socket_af_from_preferred_endpoint(
                        &out_conn->nontransient_state.preferred_endpoint),
                .last_local_port_buffer =
                        &out_conn->nontransient_state.last_local_port,
                .static_port_preference = anjay->udp_listen_port
            }, info->uri);
    if (!socket) {
        assert(result);
        anjay_log(ERROR, "could not create CoAP socket");
        return result;
    }

    anjay_log(INFO, "connected to %s:%s", info->uri->host, info->uri->port);
    out_conn->conn_socket_ = socket;
    return 0;
}

const anjay_connection_type_definition_t ANJAY_CONNECTION_DEF_UDP = {
    .name = "UDP",
    .get_connection_info = get_udp_connection_info,
    .get_net_security_info = get_udp_net_security_info,
    .create_connected_socket = create_connected_udp_socket
};
