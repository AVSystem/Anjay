/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_SERVERS_SECURITY_H
#define ANJAY_SERVERS_SECURITY_H

#include "anjay_connections.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_TEST)
#    error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    anjay_ssid_t ssid;
    anjay_iid_t security_iid;
    avs_url_t *uri;
    const anjay_transport_info_t *transport_info;
    bool is_encrypted;
    anjay_server_name_indication_t sni;
} anjay_connection_info_t;

int _anjay_connection_security_generic_get_uri(
        anjay_unlocked_t *anjay,
        anjay_iid_t security_iid,
        avs_url_t **out_uri,
        const anjay_transport_info_t **out_transport_info);

avs_error_t _anjay_connection_security_generic_get_config(
        anjay_unlocked_t *anjay,
        anjay_security_config_t *out_config,
        anjay_security_config_cache_t *cache,
        anjay_connection_info_t *inout_info);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SECURITY_H
