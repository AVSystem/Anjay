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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H
#define ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H

#include <anjay/core.h>

#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    ANJAY_CONNECTION_UNSET = -1,
    ANJAY_CONNECTION_PRIMARY = 0,
    ANJAY_CONNECTION_LIMIT_
} anjay_connection_type_t;

#define ANJAY_CONNECTION_TYPE_FOREACH(Var)                                     \
    for ((Var) = (anjay_connection_type_t) 0; (Var) < ANJAY_CONNECTION_LIMIT_; \
         (Var) = (anjay_connection_type_t) ((Var) + 1))

typedef struct {
    char pk_or_identity[ANJAY_MAX_PK_OR_IDENTITY_SIZE];
    size_t pk_or_identity_size;
    char server_pk_or_identity[ANJAY_MAX_SERVER_PK_OR_IDENTITY_SIZE];
    size_t server_pk_or_identity_size;
    char secret_key[ANJAY_MAX_SECRET_KEY_SIZE];
    size_t secret_key_size;
} anjay_server_dtls_keys_t;

/**
 * Reads security information (security mode, keys etc.) for a given Security
 * object instance. This is part of the servers subsystem because it reuses some
 * private code that is also used when refreshing server connections - namely,
 * connection_type_definition_t instances that query the data model for security
 * information, abstracting away the fact that UDP/TCP and SMS security
 * information is stored in different resources.
 *
 * It's currently only used in the Firmware Update module, to allow deriving the
 * security information from the data model when it's not explicitly specified.
 */
anjay_security_config_t *_anjay_get_security_config(anjay_t *anjay,
                                                    anjay_iid_t security_iid);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H */
