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

#ifndef ANJAY_SERVERS_SERVER_CONNECTIONS_H
#define ANJAY_SERVERS_SERVER_CONNECTIONS_H

#include "../anjay_core.h"
#include "../anjay_utils_private.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_TEST)
#    error "Headers from servers/ are not meant to be included from outside"
#endif

#include "anjay_connections.h"
#include "anjay_servers_internal.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

static inline anjay_server_connection_t *
_anjay_get_server_connection(anjay_connection_ref_t ref) {
    assert(ref.server);
    return _anjay_connection_get(&ref.server->connections, ref.conn_type);
}

bool _anjay_connections_is_trigger_requested(const char *binding_mode);

void _anjay_active_server_refresh(anjay_server_info_t *server);

void _anjay_connections_flush_notifications(anjay_connections_t *connections);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SERVER_CONNECTIONS_H
