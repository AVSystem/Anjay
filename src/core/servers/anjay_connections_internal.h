/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
anjay_connection_get_dtls_handshake_timeouts_t(anjay_unlocked_t *anjay);

typedef avs_error_t anjay_connection_prepare_t(
        anjay_unlocked_t *anjay,
        anjay_server_connection_t *out_connection,
        const avs_net_ssl_configuration_t *socket_config,
        const avs_net_socket_dane_tlsa_record_t *dane_tlsa_record,
        const anjay_connection_info_t *info);

typedef avs_error_t
anjay_connection_connect_socket_t(anjay_unlocked_t *anjay,
                                  anjay_server_connection_t *connection);

typedef int
anjay_connection_ensure_coap_context_t(anjay_unlocked_t *anjay,
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
#if defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
extern const anjay_connection_type_definition_t ANJAY_CONNECTION_DEF_TCP;
#endif // defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)

avs_error_t
_anjay_dm_read_security_info(anjay_unlocked_t *anjay,
                             anjay_iid_t security_iid,
                             anjay_rid_t security_rid,
                             avs_crypto_security_info_tag_t tag,
                             avs_crypto_security_info_union_t **out_array,
                             size_t *out_element_count);

avs_error_t
_anjay_connection_init_psk_security(anjay_unlocked_t *anjay,
                                    anjay_iid_t security_iid,
                                    anjay_rid_t identity_rid,
                                    anjay_rid_t secret_key_rid,
                                    avs_net_security_info_t *security,
                                    anjay_security_config_cache_t *cache);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_SERVERS_CONNECTIONS_INTERNAL_H */
