/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_SERVERS_CONNECTIONS_H
#define ANJAY_SERVERS_CONNECTIONS_H

#include "../anjay_core.h"
#include "../utils_core.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_TEST)
#    error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    avs_net_resolved_endpoint_t preferred_endpoint;
    char dtls_session_buffer[ANJAY_DTLS_SESSION_BUFFER_SIZE];
    char last_local_port[ANJAY_MAX_URL_PORT_SIZE];
} anjay_server_connection_nontransient_state_t;

typedef enum {
    /**
     * _anjay_connections_refresh() has just been called, and the connection has
     * not yet reached a usable state.
     */
    ANJAY_SERVER_CONNECTION_IN_PROGRESS,

    /**
     * If _anjay_server_on_refreshed() is called with server connection in this
     * state, it means that the connection has just entered a usable state after
     * completing the "connect" operation.
     *
     * As a consequence, it probably does not make sense to retry connecting if
     * an error occurs.
     */
    ANJAY_SERVER_CONNECTION_FRESHLY_CONNECTED,

    /**
     * If _anjay_server_on_refreshed() is called with server connection in this
     * state, it means that it is not the first time it is called for that
     * connection since it entered a usable state.
     *
     * As a consequence, it might make sense to retry connecting if an error
     * occurs and the connection is stateful.
     */
    ANJAY_SERVER_CONNECTION_STABLE,

    /**
     * Attempt to refresh the connection failed. Possible causes include:
     * - failure to read connection configuration from the data model
     * - error when creating the socket
     * - error during the "connect" operation
     */
    ANJAY_SERVER_CONNECTION_ERROR
} anjay_server_connection_state_t;

/**
 * State of a specific connection to an LwM2M server. One server entry may have
 * multiple connections, if multiple binding is used (e.g. US binding mode,
 * signifying UDP+SMS).
 */
typedef struct {
    /**
     * Cached URI of the given connection - this is exactly the value returned
     * by _anjay_connection_uri().
     */
    anjay_url_t uri;

    /**
     * Socket used for communication with the given server. Aside from being
     * used for actual communication, the value of this field is also used as
     * kind of a three-state flag:
     *
     * - When it is NULL - it means either of the three:
     *   - the server is either inactive (see docs to anjay_server_info_t for
     *     details)
     *   - initial attempt to connect the socket failed - the server may still
     *     be active if some other transport could be connected -
     *     _anjay_active_server_refresh() reschedules reload_servers_sched_job()
     *     in such case
     *   - the transport represented by this connection object is not used in
     *     the current binding
     *
     * - The socket may exist, but be offline (closed), when:
     *   - reconnection is scheduled, as part of the executions path of
     *     _anjay_schedule_server_reconnect(), anjay_schedule_reconnect() or
     *     registration_update_with_ctx() - see those functions' docs and call
     *     graphs for details
     *   - when the queue mode for this connection is used, and
     *     MAX_TRANSMIT_WAIT passed since last communication
     *   - when Client- or Server-Initiated Bootstrap is in progress - all
     *     non-Bootstrap sockets are disconnected in such a case.
     *
     *   Note that the server is still considered active if it has a created,
     *   but disconnected socket. Such closed socket still retains some of its
     *   previous state (including the remote endpoint's hostname and security
     *   keys etc.) in avs_commons' internal structures. This is used by
     *   _anjay_connection_internal_ensure_online() to reconnect the socket if
     *   necessary.
     *
     *   We cannot rely on reading the connection information from the data
     *   model instead, because it may be gone - for example when trying to
     *   De-register from a server that has just been deleted by a Bootstrap
     *   Server. At least that example was used in the docs prior to the June
     *   2018 server subsystem docs rewrite, because currently we don't seem to
     *   send Deregister messages in such a case anyway, so this might be a TODO
     *   for investigating.
     *
     * - The socket may exist and be online (ready for communication) - this is
     *   the normal, fully active state.
     */
    avs_net_abstract_socket_t *conn_socket_;
#if defined(__GNUC__) && !defined(__CC_ARM) \
        && !(defined(ANJAY_SERVERS_CONNECTION_SOURCE) || defined(ANJAY_TEST))
#    pragma GCC poison conn_socket_
#endif

    /**
     * Token that changes to a new unique value every time the CoAP endpoint
     * association (i.e., DTLS session or raw UDP socket) every time it has been
     * established anew.
     *
     * It is used to determine whether reconnect operation re-used the previous
     * association or created a new one.
     */
    anjay_conn_session_token_t session_token;

    /**
     * True if the "connect" operation on the socket involves some actual
     * network traffic. Used to determine whether it is meaningful to attempt
     * reconnection as an error recovery step.
     */
    bool stateful;

    /**
     * State of the socket connection.
     */
    anjay_server_connection_state_t state;

    /**
     * Flag that is set to true whenever the attempt to bring the socket up from
     * any other state is made. It signals that any outstanding notifications
     * shall be scheduled to send after the connection refresh is finished.
     */
    bool needs_observe_flush;

    /**
     * The part of active connection state that is intentionally NOT cleaned up
     * when deactivating the server. It contains:
     *
     * - preferred_endpoint, i.e. the preference which server IP address to use
     *   if multiple are returned during DNS resolution
     * - DTLS session cache
     * - Last bound local port
     *
     * These information will be used during the next reactivation to attempt
     * recreating the socket in a state most similar possible to how it was
     * before.
     */
    anjay_server_connection_nontransient_state_t nontransient_state;

    /**
     * Cached value of the connection mode, according to the Binding value most
     * recently read in _anjay_active_server_refresh().
     */
    anjay_server_connection_mode_t mode;

    /**
     * Handle to scheduled queue_mode_close_socket() scheduler job. Scheduled
     * by _anjay_connection_schedule_queue_mode_close().
     */
    anjay_sched_handle_t queue_mode_close_socket_clb;
} anjay_server_connection_t;

typedef struct {
    /**
     * Connection (socket, binding) entries - see docs to
     * anjay_server_connection_t for details.
     */
    anjay_server_connection_t connections_[ANJAY_CONNECTION_LIMIT_];

    /**
     * Information about which connection is currently the "primary" one. The
     * "primary" connection is the one on which the autonomous outgoing messages
     * (i.e. Register/Update or Bootstrap Request) are sent. See the docs in
     * server.h for details (Ctrl+F the word "primary").
     */
    anjay_connection_type_t primary_connection;
} anjay_connections_t;

static inline anjay_server_connection_t *
_anjay_connection_get(anjay_connections_t *connections,
                      anjay_connection_type_t conn_type) {
    assert(conn_type >= (anjay_connection_type_t) 0
           && conn_type < ANJAY_CONNECTION_LIMIT_);
    return &connections->connections_[conn_type];
}

#if defined(__GNUC__) && !defined(__CC_ARM)
#    pragma GCC poison connections_
#endif

avs_net_abstract_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection);

void _anjay_connection_internal_clean_socket(
        const anjay_t *anjay, anjay_server_connection_t *connection);

anjay_connection_type_t
_anjay_connections_get_primary(anjay_connections_t *connections);

anjay_conn_session_token_t
_anjay_connections_get_primary_session_token(anjay_connections_t *connections);

bool _anjay_connection_is_online(anjay_server_connection_t *connection);

void _anjay_connection_internal_bring_online(anjay_t *anjay,
                                             anjay_connections_t *connections,
                                             anjay_connection_type_t conn_type);

void _anjay_connections_close(const anjay_t *anjay,
                              anjay_connections_t *connections);

void _anjay_connections_refresh(anjay_t *anjay,
                                anjay_connections_t *connections,
                                anjay_iid_t security_iid,
                                const anjay_url_t *uri,
                                const char *binding_mode);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_CONNECTIONS_H
