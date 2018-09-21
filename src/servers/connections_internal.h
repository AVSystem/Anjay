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

#ifndef ANJAY_SERVERS_CONNECTIONS_INTERNAL_H
#define ANJAY_SERVERS_CONNECTIONS_INTERNAL_H

#include "../servers.h"

#include "connections.h"

#if !defined(ANJAY_TEST)                      \
        && (!defined(ANJAY_SERVERS_INTERNALS) \
            || !defined(ANJAY_SERVERS_CONNECTION_SOURCE))
#    error "connections_internal.h shall only be included from connection_*.c files"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    anjay_udp_security_mode_t security_mode;
} anjay_udp_connection_info_t;


typedef struct {
    anjay_iid_t security_iid;
    const anjay_url_t *uri;
    const char *binding_mode;
    anjay_udp_connection_info_t udp;
} anjay_connection_info_t;

typedef int
anjay_connection_get_info_t(anjay_t *anjay,
                            anjay_connection_info_t *inout_info,
                            anjay_server_dtls_keys_t *out_dtls_keys);

typedef int anjay_connection_get_net_security_info_t(
        avs_net_security_info_t *out_net_info,
        const anjay_connection_info_t *info,
        const anjay_server_dtls_keys_t *dtls_keys);

typedef int anjay_connection_create_connected_socket_t(
        anjay_t *anjay,
        anjay_server_connection_t *out_connection,
        avs_net_ssl_configuration_t *inout_socket_config,
        const anjay_connection_info_t *info);

typedef struct {
    const char *name;
    anjay_connection_get_info_t *get_connection_info;
    anjay_connection_get_net_security_info_t *get_net_security_info;
    anjay_connection_create_connected_socket_t *create_connected_socket;
} anjay_connection_type_definition_t;

extern const anjay_connection_type_definition_t ANJAY_CONNECTION_DEF_UDP;

int _anjay_connection_init_psk_security(avs_net_security_info_t *security,
                                        const anjay_server_dtls_keys_t *keys);

avs_net_af_t _anjay_socket_af_from_preferred_endpoint(
        const avs_net_resolved_endpoint_t *endpoint);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_SERVERS_CONNECTIONS_INTERNAL_H */
