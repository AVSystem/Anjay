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

#ifndef ANJAY_SERVERS_CONNECTIONS_H
#define ANJAY_SERVERS_CONNECTIONS_H

#include "../anjay_core.h"
#include "../anjay_utils_private.h"

#include <avsystem/commons/avs_url.h>

#include <avsystem/coap/ctx.h>

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
     * Connection is offline. Possible causes include:
     * - failure to read connection configuration from the data model
     * - error when creating the socket
     * - error during the "connect" operation
     * - none of the supported transports is available
     */
    ANJAY_SERVER_CONNECTION_OFFLINE
} anjay_server_connection_state_t;

/**
 * State of a specific connection to an LwM2M server. One server entry may have
 * up to 2 connections, if the SMS trigger feature is used.
 */
typedef struct {
    /**
     * Cached URI of the given connection - this is exactly the value returned
     * by _anjay_connection_uri().
     */
    anjay_url_t uri;

    /**
     * CoAP transport layer type (UDP/TCP/SMS etc.). Initialized during socket
     * refresh, Used to select an appropriate connection_def and CoAP context
     * type.
     */
    anjay_socket_transport_t transport;

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
    avs_net_socket_t *conn_socket_;
#if defined(__GNUC__) && !defined(__CC_ARM) \
        && !(defined(ANJAY_SERVERS_CONNECTION_SOURCE) || defined(ANJAY_TEST))
#    pragma GCC poison conn_socket_
#endif

    avs_coap_ctx_t *coap_ctx;

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
     * Handle to scheduled queue_mode_close_socket() scheduler job. Scheduled
     * by _anjay_connection_schedule_queue_mode_close().
     */
    avs_sched_handle_t queue_mode_close_socket_clb;
} anjay_server_connection_t;

typedef struct {
    /**
     * Connection (socket, binding) entries - see docs to
     * anjay_server_connection_t for details.
     */
    anjay_server_connection_t connections_[ANJAY_CONNECTION_LIMIT_];
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

avs_net_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection);

void _anjay_connection_internal_clean_socket(
        anjay_t *anjay, anjay_server_connection_t *connection);

anjay_conn_session_token_t
_anjay_connections_get_primary_session_token(anjay_connections_t *connections);

void _anjay_connection_internal_invalidate_session(
        anjay_server_connection_t *connection);

static inline bool
_anjay_connection_is_online(anjay_server_connection_t *connection) {
    return _anjay_socket_is_online(
            _anjay_connection_internal_get_socket(connection));
}

avs_error_t _anjay_server_connection_internal_bring_online(
        anjay_server_info_t *server,
        anjay_connection_type_t conn_type,
        const anjay_iid_t *security_iid);

void _anjay_connections_close(anjay_t *anjay, anjay_connections_t *connections);

typedef struct {
    char sni[256];
} anjay_server_name_indication_t;

/**
 * Makes sure that socket connections for a given server are up-to-date with the
 * current configuration; (re)connects any sockets and schedules Register/Update
 * operations as necessary.
 *
 * Any errors are reported by calling _anjay_connections_on_refreshed().
 *
 * @param server            Server information object to operate on.
 *
 * @param security_iid      Security Object Instance ID related to the server
 *                          being refreshed.
 *
 * @param move_uri          Pointer to a server URL to connect to. This function
 *                          will take ownership of data allocated inside that
 *                          object.
 *
 * @param trigger_requested True if SMS Trigger connection is supposed to be
 *                          used in addition to the primary connection. In the
 *                          current version, the value of this argument will be
 *                          ignored if the primary connection uses SMS
 *                          transport.
 *
 * @param sni               Server Name Identification value to be used for
 *                          certificate validation during TLS handshake.
 */
void _anjay_server_connections_refresh(
        anjay_server_info_t *server,
        anjay_iid_t security_iid,
        avs_url_t **move_uri,
        bool trigger_requested,
        const anjay_server_name_indication_t *sni);

bool _anjay_socket_transport_supported(anjay_t *anjay,
                                       anjay_socket_transport_t type);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_CONNECTIONS_H
