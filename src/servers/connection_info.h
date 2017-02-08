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

#ifndef ANJAY_SERVERS_CONNECTION_INFO_H
#define	ANJAY_SERVERS_CONNECTION_INFO_H

#include "../anjay.h"
#include "../utils.h"

#ifndef ANJAY_SERVERS_INTERNALS
#error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

static inline avs_net_abstract_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection) {
    return (avs_net_abstract_socket_t *) connection->private_data;
}

static inline void _anjay_connection_internal_set_move_socket(
        anjay_server_connection_t *connection,
        avs_net_abstract_socket_t **move_socket) {
    avs_net_socket_cleanup(
            (avs_net_abstract_socket_t **) &connection->private_data);
    if (move_socket) {
        connection->private_data = *move_socket;
        *move_socket = NULL;
    }
}

int _anjay_server_refresh(anjay_t *anjay,
                          anjay_active_server_info_t *server,
                          bool force_reconnect);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_CONNECTION_INFO_H
