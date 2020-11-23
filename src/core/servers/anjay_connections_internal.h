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

#ifndef ANJAY_SERVERS_CONNECTIONS_INTERNAL_H
#define ANJAY_SERVERS_CONNECTIONS_INTERNAL_H

#include "../anjay_servers_private.h"

#include "anjay_connections.h"
#include "anjay_security.h"

#if !defined(ANJAY_TEST)                      \
        && (!defined(ANJAY_SERVERS_INTERNALS) \
            || !defined(ANJAY_SERVERS_CONNECTION_SOURCE))
#    error "connections_internal.h shall only be included from connection_*.c or security_*.c files"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

static inline void
_anjay_connection_info_cleanup(anjay_connection_info_t *info) {
    avs_url_free(info->uri);
    info->uri = NULL;
    info->transport_info = NULL;
}

typedef const avs_net_dtls_handshake_timeouts_t *
anjay_connection_get_dtls_handshake_timeouts_t(anjay_t *anjay);

typedef avs_error_t anjay_connection_prepare_t(
        anjay_t *anjay,
        anjay_server_connection_t *out_connection,
        const avs_net_ssl_configuration_t *socket_config,
        const avs_net_socket_dane_tlsa_record_t *dane_tlsa_record,
        const anjay_connection_info_t *info);

typedef avs_error_t
anjay_connection_connect_socket_t(anjay_t *anjay,
                                  anjay_server_connection_t *connection);

typedef int
anjay_connection_ensure_coap_context_t(anjay_t *anjay,
                                       anjay_server_connection_t *connection);

typedef struct {
    const char *name;
    anjay_connection_get_dtls_handshake_timeouts_t *get_dtls_handshake_timeouts;
    anjay_connection_prepare_t *prepare_connection;
    anjay_connection_ensure_coap_context_t *ensure_coap_context;
    anjay_connection_connect_socket_t *connect_socket;
} anjay_connection_type_definition_t;

#ifdef WITH_AVS_COAP_UDP
extern const anjay_connection_type_definition_t ANJAY_CONNECTION_DEF_UDP;
#endif // WITH_AVS_COAP_UDP

avs_error_t
_anjay_connection_init_psk_security(anjay_t *anjay,
                                    anjay_iid_t security_iid,
                                    anjay_rid_t identity_rid,
                                    anjay_rid_t secret_key_rid,
                                    avs_net_security_info_t *security,
                                    void **out_psk_buffer);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_SERVERS_CONNECTIONS_INTERNAL_H */
