/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <config.h>

#include <inttypes.h>

#include <avsystem/commons/stream/net.h>

#include "../dm/query.h"

#define ANJAY_SERVERS_INTERNALS

#include "connection_info.h"

VISIBILITY_SOURCE_BEGIN

#define EMPTY_SERVER_INFO_INITIALIZER \
        { \
            .udp = { \
                .keys = { \
                    .pk_or_identity = ANJAY_RAW_BUFFER_ON_STACK( \
                            ANJAY_MAX_PK_OR_IDENTITY_SIZE), \
                    .server_pk_or_identity = ANJAY_RAW_BUFFER_ON_STACK( \
                            ANJAY_MAX_SERVER_PK_OR_IDENTITY_SIZE), \
                    .secret_key = ANJAY_RAW_BUFFER_ON_STACK( \
                            ANJAY_MAX_SECRET_KEY_SIZE) \
                } \
            } \
        }

typedef struct {
    anjay_raw_buffer_t pk_or_identity;
    anjay_raw_buffer_t server_pk_or_identity;
    anjay_raw_buffer_t secret_key;
} dtls_keys_t;

#define MAX_PORT_LENGTH sizeof("65535")
typedef struct {
    anjay_server_connection_mode_t mode;
    anjay_url_t uri;
    char local_port[MAX_PORT_LENGTH];
    anjay_udp_security_mode_t security_mode;
    dtls_keys_t keys;
} udp_connection_info_t;

typedef struct {
    anjay_iid_t security_iid;
    anjay_ssid_t ssid; // or ANJAY_SSID_BOOTSTRAP
    udp_connection_info_t udp;
} server_connection_info_t;

static anjay_binding_mode_t read_binding_mode(anjay_t *anjay,
                                              anjay_ssid_t ssid) {
    char buf[8];
    anjay_resource_path_t path = {
        ANJAY_DM_OID_SERVER, ANJAY_IID_INVALID, ANJAY_DM_RID_SERVER_BINDING
    };

    if (!_anjay_find_server_iid(anjay, ssid, &path.iid)
            && !_anjay_dm_res_read_string(anjay, &path, buf, sizeof(buf))) {
        return anjay_binding_mode_from_str(buf);
    } else {
        anjay_log(WARNING, "could not read binding mode for LWM2M server %u",
                  ssid);
        // pass through
    }
    return ANJAY_BINDING_NONE;
}

static struct {
    anjay_binding_mode_t binding;
    struct {
        anjay_server_connection_mode_t udp;
        anjay_server_connection_mode_t sms;
    } connection;
} BINDING_TO_CONNECTIONS[] = {
    { ANJAY_BINDING_U,   { .udp = ANJAY_CONNECTION_ONLINE,
                           .sms = ANJAY_CONNECTION_DISABLED } },
    { ANJAY_BINDING_UQ,  { .udp = ANJAY_CONNECTION_QUEUE,
                           .sms = ANJAY_CONNECTION_DISABLED } },
    { ANJAY_BINDING_S,   { .udp = ANJAY_CONNECTION_DISABLED,
                           .sms = ANJAY_CONNECTION_ONLINE } },
    { ANJAY_BINDING_SQ,  { .udp = ANJAY_CONNECTION_DISABLED,
                           .sms = ANJAY_CONNECTION_QUEUE } },
    { ANJAY_BINDING_US,  { .udp = ANJAY_CONNECTION_ONLINE,
                           .sms = ANJAY_CONNECTION_ONLINE } },
    { ANJAY_BINDING_UQS, { .udp = ANJAY_CONNECTION_QUEUE,
                           .sms = ANJAY_CONNECTION_ONLINE } }
};

static int read_connection_modes(anjay_t *anjay,
                                 anjay_ssid_t ssid,
                                 anjay_server_connection_mode_t *out_udp_mode,
                                 anjay_server_connection_mode_t *out_sms_mode) {
    if (ssid != ANJAY_SSID_BOOTSTRAP) {
        anjay_binding_mode_t binding_mode = read_binding_mode(anjay, ssid);
        for (size_t i = 0; i < ANJAY_ARRAY_SIZE(BINDING_TO_CONNECTIONS); ++i) {
            if (BINDING_TO_CONNECTIONS[i].binding == binding_mode) {
                *out_udp_mode = BINDING_TO_CONNECTIONS[i].connection.udp;
                *out_sms_mode = BINDING_TO_CONNECTIONS[i].connection.sms;
                return 0;
            }
        }
        anjay_log(ERROR, "could not read binding mode");
        return -1;
    } else {
        *out_udp_mode = ANJAY_CONNECTION_ONLINE;
        // TODO: *out_udp_mode if there is SMS support
        *out_sms_mode = ANJAY_CONNECTION_DISABLED;
        return 0;
    }
}

static anjay_server_connection_mode_t
current_connection_mode(const anjay_server_connection_t *connection) {
    if (_anjay_connection_internal_get_socket(connection)) {
        if (connection->queue_mode) {
            return ANJAY_CONNECTION_QUEUE;
        } else {
            return ANJAY_CONNECTION_ONLINE;
        }
    } else {
        return ANJAY_CONNECTION_DISABLED;
    }
}

static anjay_binding_mode_t
binding_mode_from_connection_modes(anjay_server_connection_mode_t udp_mode,
                                   anjay_server_connection_mode_t sms_mode) {
    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(BINDING_TO_CONNECTIONS); ++i) {
        if (BINDING_TO_CONNECTIONS[i].connection.udp == udp_mode
                && BINDING_TO_CONNECTIONS[i].connection.sms == sms_mode) {
            return BINDING_TO_CONNECTIONS[i].binding;
        }
    }
    return ANJAY_BINDING_NONE;
}

anjay_binding_mode_t
_anjay_server_cached_binding_mode(const anjay_active_server_info_t *server) {
    if (!server) {
        return ANJAY_BINDING_NONE;
    }
    return binding_mode_from_connection_modes(
            current_connection_mode(&server->udp_connection),
            ANJAY_CONNECTION_DISABLED);
}

static int init_psk_security(avs_net_security_info_t *security,
                             const dtls_keys_t *keys) {
    *security = avs_net_security_info_from_psk(
        (avs_net_psk_t){
            .psk = keys->secret_key.data,
            .psk_size = keys->secret_key.size,
            .identity = keys->pk_or_identity.data,
            .identity_size = keys->pk_or_identity.size
        });
    return 0;
}

static int init_cert_security(avs_net_security_info_t *security,
                              const dtls_keys_t *keys) {
    avs_net_client_cert_t client_cert =
            avs_net_client_cert_from_x509(keys->pk_or_identity.data,
                                          keys->pk_or_identity.size);

    avs_net_private_key_t private_key =
            avs_net_private_key_from_pkcs8(keys->secret_key.data,
                                           keys->secret_key.size, NULL);

    const void *raw_cert_der = keys->server_pk_or_identity.size > 0
            ? keys->server_pk_or_identity.data
            : NULL;
    avs_net_trusted_cert_source_t ca = avs_net_trusted_cert_source_from_x509(
            raw_cert_der, keys->server_pk_or_identity.size);

    *security = avs_net_security_info_from_certificates(
            (avs_net_certificate_info_t) {
                .server_cert_validation = !!raw_cert_der,
                .trusted_certs = ca,
                .client_cert = client_cert,
                .client_key = private_key
            });

    return 0;
}

static int fill_socket_config(anjay_t *anjay,
                              avs_net_ssl_configuration_t *config,
                              const udp_connection_info_t *udp_info) {
    memset(config, 0, sizeof(*config));
    config->version = anjay->dtls_version;
    config->backend_configuration.reuse_addr = 1;

    switch (udp_info->security_mode) {
    case ANJAY_UDP_SECURITY_NOSEC:
        return 0;
    case ANJAY_UDP_SECURITY_PSK:
        return init_psk_security(&config->security, &udp_info->keys);
    case ANJAY_UDP_SECURITY_CERTIFICATE:
        return init_cert_security(&config->security, &udp_info->keys);
    case ANJAY_UDP_SECURITY_RPK:
    default:
        anjay_log(ERROR, "unsupported security mode: %d",
                  (int) udp_info->security_mode);
        return -1;
    }
}

static int get_security_mode(anjay_t *anjay,
                             anjay_iid_t security_iid,
                             anjay_udp_security_mode_t *out_mode) {
    int64_t mode;
    const anjay_resource_path_t path = {
        ANJAY_DM_OID_SECURITY, security_iid, ANJAY_DM_RID_SECURITY_MODE
    };

    if (_anjay_dm_res_read_i64(anjay, &path, &mode)) {
        anjay_log(ERROR, "could not read LWM2M server security mode");
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

static bool is_valid_coap_uri(const anjay_url_t *uri,
                              bool use_nosec) {
    if ((use_nosec && strcmp(uri->protocol, "coap"))
            || (!use_nosec && strcmp(uri->protocol, "coaps"))) {
        anjay_log(ERROR, "unsupported protocol: %s (NoSec %s)", uri->protocol,
                  use_nosec ? "enabled" : "disabled");
        return false;
    }

    if (!*uri->port) {
        anjay_log(ERROR, "missing port in LWM2M server URI");
        return false;
    }

    return true;
}

static int get_server_uri(anjay_t *anjay,
                          anjay_iid_t security_iid,
                          anjay_udp_security_mode_t security_mode,
                          anjay_url_t *out_uri) {
    enum { MAX_SERVER_URI_LENGTH = 256 };
    char raw_uri[MAX_SERVER_URI_LENGTH];

    const anjay_resource_path_t path = {
        ANJAY_DM_OID_SECURITY, security_iid, ANJAY_DM_RID_SECURITY_SERVER_URI
    };

    if (_anjay_dm_res_read_string(anjay, &path, raw_uri, sizeof(raw_uri))) {
        anjay_log(ERROR, "could not read LWM2M server URI");
        return -1;
    }

    const bool use_nosec = (security_mode == ANJAY_UDP_SECURITY_NOSEC);
    if (_anjay_parse_url(raw_uri, out_uri)
            || !is_valid_coap_uri(out_uri, use_nosec)) {
        anjay_log(ERROR, "could not parse LWM2M server URI: %s", raw_uri);
        return -1;
    }
    return 0;
}

static int get_dtls_keys(anjay_t *anjay,
                         anjay_iid_t security_iid,
                         anjay_udp_security_mode_t security_mode,
                         dtls_keys_t *out_keys) {
    if (security_mode == ANJAY_UDP_SECURITY_NOSEC) {
        return 0;
    }

    const struct {
        bool required;
        anjay_rid_t rid;
        anjay_raw_buffer_t *buffer;
    } values[] = {
        {
            true,
            ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY,
            &out_keys->pk_or_identity
        }, {
            security_mode != ANJAY_UDP_SECURITY_PSK,
            ANJAY_DM_RID_SECURITY_SERVER_PK_OR_IDENTITY,
            &out_keys->server_pk_or_identity
        }, {
            true,
            ANJAY_DM_RID_SECURITY_SECRET_KEY,
            &out_keys->secret_key
        }
    };

    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(values); ++i) {
        anjay_raw_buffer_t *value = values[i].buffer;
        const anjay_resource_path_t path = {
            ANJAY_DM_OID_SECURITY, security_iid, values[i].rid
        };
        if (_anjay_dm_res_read(anjay, &path, (char*)value->data,
                               value->capacity, &value->size)
                && values[i].required) {
            anjay_log(WARNING, "read %s failed", ANJAY_RES_PATH_STRING(&path));
            return -1;
        }
    }

    return 0;
}

static int get_udp_connection_info(anjay_t *anjay,
                                   anjay_iid_t security_iid,
                                   udp_connection_info_t *out_info) {
    return (get_security_mode(anjay, security_iid, &out_info->security_mode)
            || get_server_uri(anjay, security_iid,
                              out_info->security_mode, &out_info->uri)
            || get_dtls_keys(anjay, security_iid,
                             out_info->security_mode, &out_info->keys))
            ? -1 : 0;
}

static void get_requested_local_port(char out_port[static MAX_PORT_LENGTH],
                                     anjay_t *anjay,
                                     avs_net_abstract_socket_t *socket) {
    if (socket) {
        if (!avs_net_socket_get_local_port(socket, out_port,
                                           MAX_PORT_LENGTH)) {
            return;
        }

        anjay_log(DEBUG, "could not read local port from old socket");
    }

    if (_anjay_snprintf(out_port, MAX_PORT_LENGTH, "%s", anjay->udp_port) > 0) {
        return;
    }

    out_port[0] = '\0';
}

static int get_connection_info(anjay_t *anjay,
                               anjay_ssid_t ssid,
                               avs_net_abstract_socket_t *old_socket,
                               server_connection_info_t *out_info) {
    if (_anjay_find_security_iid(anjay, ssid, &out_info->security_iid)) {
        anjay_log(ERROR, "could not find server Security IID");
        return -1;
    }

    out_info->ssid = ssid;

    anjay_server_connection_mode_t sms_mode;
    if (read_connection_modes(anjay, ssid, &out_info->udp.mode, &sms_mode)
            || sms_mode != ANJAY_CONNECTION_DISABLED) {
        return -1;
    }

    if (out_info->udp.mode != ANJAY_CONNECTION_DISABLED) {
        if (get_udp_connection_info(anjay, out_info->security_iid,
                                    &out_info->udp)) {
            return -1;
        }

        get_requested_local_port(out_info->udp.local_port, anjay, old_socket);

        anjay_log(DEBUG,
                  "server %u: /%u/%u: %s://%s:%s, local port %s, "
                  "security mode = %d",
                  ssid, ANJAY_DM_OID_SECURITY, out_info->security_iid,
                  out_info->udp.uri.protocol, out_info->udp.uri.host,
                  out_info->udp.uri.port, out_info->udp.local_port,
                  (int) out_info->udp.security_mode);
    }

    return 0;
}

static avs_net_abstract_socket_t *
create_connected_udp_socket(anjay_t *anjay, const udp_connection_info_t *info) {
    avs_net_abstract_socket_t *socket = NULL;
    avs_net_socket_type_t type =
            info->security_mode == ANJAY_UDP_SECURITY_NOSEC
                ? AVS_NET_UDP_SOCKET : AVS_NET_DTLS_SOCKET;

    avs_net_ssl_configuration_t config;
    if (fill_socket_config(anjay, &config, info)) {
        goto error;
    }

    const void *config_ptr = (type == AVS_NET_DTLS_SOCKET)
            ? (const void *) &config
            : (const void *) &config.backend_configuration;

    if (avs_net_socket_create(&socket, type, config_ptr)) {
        anjay_log(ERROR, "could not create CoAP socket");
        goto error;
    }

    if (*info->local_port
            && avs_net_socket_bind(socket, NULL, info->local_port)) {
        anjay_log(ERROR, "could not bind socket to port %s",
                  info->local_port);
        goto error;
    }

    if (avs_net_socket_connect(socket, info->uri.host, info->uri.port)) {
        anjay_log(ERROR, "could not connect to %s:%s",
                  info->uri.host, info->uri.port);
        goto error;
    }

    anjay_log(INFO, "connected to %s:%s", info->uri.host, info->uri.port);
    return socket;
error:
    avs_net_socket_cleanup(&socket);
    return NULL;
}

static int refresh_udp(anjay_t *anjay,
                       anjay_server_connection_t *out_connection,
                       const udp_connection_info_t *info) {
    avs_net_abstract_socket_t *new_socket = NULL;
    bool currently_connected =
            (_anjay_connection_internal_get_socket(out_connection) != NULL);
    bool should_be_connected = (info->mode != ANJAY_CONNECTION_DISABLED);
    if (currently_connected != should_be_connected
            || out_connection->needs_socket_update) {
        _anjay_connection_internal_set_move_socket(out_connection, NULL);
        if (should_be_connected) {
            if (!(new_socket = create_connected_udp_socket(anjay, info))) {
                return -1;
            } else {
                _anjay_connection_internal_set_move_socket(out_connection,
                                                           &new_socket);
            }
        }
    }
    out_connection->needs_socket_update = false;
    out_connection->needs_discard_old_packets = false;
    out_connection->queue_mode = (info->mode == ANJAY_CONNECTION_QUEUE);
    return 0;
}

int _anjay_server_refresh(anjay_t *anjay,
                          anjay_active_server_info_t *server,
                          bool force_reconnect) {
    anjay_log(TRACE, "refreshing SSID %u, force_reconnect == %d",
              server->ssid, (int) force_reconnect);

    avs_net_abstract_socket_t *old_socket =
            _anjay_connection_internal_get_socket(&server->udp_connection);
    server_connection_info_t server_info = EMPTY_SERVER_INFO_INITIALIZER;
    if (get_connection_info(anjay, server->ssid, old_socket, &server_info)) {
        anjay_log(DEBUG, "could not get connection info for SSID %u",
                  server->ssid);
        return -1;
    }

    if (force_reconnect) {
        server->udp_connection.needs_socket_update = true;
    }

    return refresh_udp(anjay, &server->udp_connection, &server_info.udp);
}
