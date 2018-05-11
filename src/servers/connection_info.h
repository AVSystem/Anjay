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

#ifndef ANJAY_SERVERS_CONNECTION_INFO_H
#define ANJAY_SERVERS_CONNECTION_INFO_H

#include "../anjay_core.h"
#include "../utils_core.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_TEST)
#error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    avs_net_resolved_endpoint_t preferred_endpoint;
    char dtls_session_buffer[ANJAY_DTLS_SESSION_BUFFER_SIZE];
    char last_local_port[ANJAY_MAX_URL_PORT_SIZE];
} anjay_server_connection_nontransient_state_t;

typedef struct {
    /**
     * If queue mode is in use, this socket may be non-NULL, but closed (by
     * means of <c>avs_net_socket_close()</c>). Such closed socket still retains
     * some of its previous state (including the remote endpoint's hostname and
     * security keys etc.) in avs_commons' internal structures.
     *
     * This is used by <c>_anjay_connection_internal_ensure_online()</c> to
     * reconnect the socket if necessary.
     *
     * We cannot rely on reading the connection information from data model
     * instead, because it may be gone - for example when trying to De-register
     * from a server that has just been deleted by a Bootstrap Server.
     */
    avs_net_abstract_socket_t *conn_socket_;
#if defined(__GNUC__) \
        && !(defined(ANJAY_SERVERS_CONNECTION_INFO_C) || defined(ANJAY_TEST))
#pragma GCC poison conn_socket_
#endif

    anjay_server_connection_nontransient_state_t nontransient_state;

    bool needs_reconnect;

    bool queue_mode;
    anjay_sched_handle_t queue_mode_close_socket_clb_handle;
} anjay_server_connection_t;

anjay_server_connection_t *
_anjay_get_server_connection(anjay_connection_ref_t ref);

avs_net_abstract_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection);

void
_anjay_connection_internal_clean_socket(anjay_server_connection_t *connection);

bool _anjay_connection_is_online(anjay_server_connection_t *connection);

int
_anjay_connection_internal_bring_online(anjay_server_connection_t *connection,
                                        bool *out_session_resumed);

/**
 * @returns @li 0 on success,
 *          @li a positive errno value in case of a primary socket (UDP) error,
 *          @li a negative value in case of other error.
 */
int _anjay_active_server_refresh(anjay_t *anjay,
                                 anjay_server_info_t *server,
                                 bool force_reconnect);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_CONNECTION_INFO_H
