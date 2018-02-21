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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H
#define ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H

#include <anjay/core.h>

#include <anjay_modules/utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    ANJAY_CONNECTION_UDP,
    ANJAY_CONNECTION_SMS,
    ANJAY_CONNECTION_UNSET
} anjay_connection_type_t;

typedef struct {
    char pk_or_identity[ANJAY_MAX_PK_OR_IDENTITY_SIZE];
    size_t pk_or_identity_size;
    char server_pk_or_identity[ANJAY_MAX_SERVER_PK_OR_IDENTITY_SIZE];
    size_t server_pk_or_identity_size;
    char secret_key[ANJAY_MAX_SECRET_KEY_SIZE];
    size_t secret_key_size;
} anjay_server_dtls_keys_t;

int _anjay_server_get_uri(anjay_t *anjay,
                          anjay_iid_t security_iid,
                          anjay_url_t *out_uri);

int _anjay_get_security_info(anjay_t *anjay,
                             avs_net_security_info_t *out_net_info,
                             anjay_server_dtls_keys_t *out_dtls_keys,
                             anjay_iid_t security_iid,
                             anjay_connection_type_t conn_type);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H */
