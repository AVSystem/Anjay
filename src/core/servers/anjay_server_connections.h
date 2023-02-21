/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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

void _anjay_active_server_refresh(anjay_server_info_t *server);

void _anjay_connections_flush_notifications(anjay_connections_t *connections);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SERVER_CONNECTIONS_H
