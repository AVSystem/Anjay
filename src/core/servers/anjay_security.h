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
        anjay_t *anjay,
        anjay_iid_t security_iid,
        avs_url_t **out_uri,
        const anjay_transport_info_t **out_transport_info);

avs_error_t _anjay_connection_security_generic_get_config(
        anjay_t *anjay,
        anjay_security_config_t *out_config,
        anjay_security_config_cache_t *cache,
        anjay_connection_info_t *inout_info);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SECURITY_H
