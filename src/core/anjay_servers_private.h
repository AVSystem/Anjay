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

#ifndef ANJAY_SERVERS_PRIVATE_H
#define ANJAY_SERVERS_PRIVATE_H

#include <anjay/core.h>

#include <anjay_modules/anjay_sched.h>
#include <anjay_modules/anjay_servers.h>

#include <avsystem/commons/avs_persistence.h>
#include <avsystem/commons/avs_time.h>

#include <avsystem/coap/ctx.h>

#include "anjay_utils_private.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

////////////////////////////////////////////////////////////////////////////////
///
/// This is the API of the servers subsystem. Any of the files outside the
/// servers/ subdirectory are ONLY supposed to call:
///
/// - APIs in this file
/// - APIs in <anjay_modules/servers.h>
/// - public APIs that are implemented inside the servers/ subdirectory - they
///   can be queried using the following command after compilation:
///
///       nm CMakeFiles/anjay.dir/src/servers/*.o | grep ' T anjay'
///
///   and at the time of writing this documentation, consist of:
///
///     - anjay_all_connections_failed()
///     - anjay_disable_server()
///     - anjay_disable_server_with_timeout()
///     - anjay_enable_server()
///     - anjay_enter_offline()
///     - anjay_exit_offline()
///     - anjay_get_socket_entries()
///     - anjay_is_offline()
///     - anjay_schedule_reconnect()
///     - anjay_schedule_registration_update()
///
/// As the documentation in public headers is written as a user's manual, the
/// technical/developer documentation for public functions is written in the
/// implementation files (*.c).
///
/// You may also want to look at servers_internal.h for data structure
/// documentation.
///
////////////////////////////////////////////////////////////////////////////////

/**
 * Token that changes to a new unique value every time the CoAP endpoint
 * association (i.e., DTLS session or raw UDP socket) has been established anew.
 *
 * It is currently implemented as a monotonic timestamp because it's trivial to
 * generate such unique value that way as long as it is never persisted.
 */
typedef struct {
    avs_time_monotonic_t value;
} anjay_conn_session_token_t;

static inline void
_anjay_conn_session_token_reset(anjay_conn_session_token_t *out) {
    out->value = avs_time_monotonic_now();
}

static inline bool
_anjay_conn_session_tokens_equal(anjay_conn_session_token_t left,
                                 anjay_conn_session_token_t right) {
    return avs_time_monotonic_equal(left.value, right.value);
}

// copied from deps/avs_commons/http/src/headers.h
// see description there for rationale; TODO: move this to public Commons API?
// note that this _includes_ the terminating null byte
#define ANJAY_UINT_STR_BUF_SIZE(type) ((12 * sizeof(type)) / 5 + 2)

// 6.2.2 Object Version format:
// "The Object Version of an Object is composed of 2 digits separated by a dot"
// However, we're a bit lenient to support proper numbers and not just digits.
#define ANJAY_DM_OBJECT_VERSION_BUF_LENGTH \
    (2 * ANJAY_UINT_STR_BUF_SIZE(unsigned))

typedef struct {
    anjay_oid_t oid;
    char version[ANJAY_DM_OBJECT_VERSION_BUF_LENGTH];
    AVS_LIST(anjay_iid_t) instances;
} anjay_dm_cache_object_t;

typedef struct {
    int64_t lifetime_s;
    char *dm;
    anjay_binding_mode_t binding_mode;
} anjay_update_parameters_t;

typedef struct {
    anjay_conn_session_token_t session_token;
    AVS_LIST(const anjay_string_t) endpoint_path;
    anjay_lwm2m_version_t lwm2m_version;
    bool queue_mode;
    avs_time_real_t expire_time;

    /**
     * This flag is set whenever the Update request is forced to be sent, either
     * manually using anjay_schedule_registration_update(), or through a
     * scheduler job that executes near lifetime expiration.
     */
    bool update_forced;

    anjay_update_parameters_t last_update_params;
} anjay_registration_info_t;

////////////////////////////////////////////////////////////////////////////////
// METHODS ON THE WHOLE SERVERS SUBSYSTEM //////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/**
 * Creates and initializes the servers structure. Currently implemented simply
 * as a zero-filling allocation.
 */
anjay_servers_t *_anjay_servers_create(void);

#ifndef ANJAY_WITHOUT_DEREGISTER
/**
 * Deregisters from every active server. It is currently only ever called from
 * anjay_delete_impl(), because there are two flavours of anjay_delete() - the
 * regular anjay_delete() is supposed to deregister from all servers on exit,
 * but anjay_delete_with_core_persistence() (present only in the commercial
 * version) shall not deregister from anywhere, because it would defeat its
 * purpose.
 *
 * We could have _anjay_servers_cleanup() with a flag, but we decided that
 * having a separate function for de-registration is more elegant.
 */
void _anjay_servers_deregister(anjay_t *anjay);
#else // ANJAY_WITHOUT_DEREGISTER
#    define _anjay_servers_deregister(Anjay) ((void) (Anjay))
#endif // ANJAY_WITHOUT_DEREGISTER

/**
 * Clears up the servers struct, and releases any allocated resources.
 *
 * It takes the whole anjay object as argument, because it needs to delete
 * scheduler jobs (so it needs the scheduler reference from somewhere), but it's
 * basically a fancy destructor for anjay->servers.
 *
 * Only ever called from anjay_delete_impl().
 */
void _anjay_servers_cleanup(anjay_t *anjay);

/**
 * Removes all references to inactive servers (see docs for anjay_server_info_t
 * above for an information what is considered "active") from internal
 * structures.
 *
 * This is currently only called from start_bootstrap_if_not_already_started().
 * Inactive servers are removed during the bootstrap process - this unschedules
 * all reactivation jobs and generally prevents the inactive servers from
 * interfering in any way. The inactive servers will be recreated (and most
 * likely, scheduled to be reactivated, as an inactive server is basically a
 * pathological case) during the next "reload" job.
 *
 * This was introduced in internal diff D5678, specifically to address the case
 * that connections which failed registration attempts were retrying during the
 * bootstrap procedure, which we were implementing as fallback. Probably there
 * are other ways to achieve the same thing.
 */
void _anjay_servers_cleanup_inactive(anjay_t *anjay);

typedef int anjay_servers_foreach_ssid_handler_t(anjay_t *anjay,
                                                 anjay_ssid_t ssid,
                                                 void *data);

/**
 * Iterates over ALL servers known to the server subsystem (note that they are
 * cached since last reload, the data model is NOT queried directly) and returns
 * their SSIDs.
 *
 * Currently used in two places:
 * - access_control_utils.c :: is_single_ssid_environment() - to determine
 *   whether Access Control is even applicable
 * - _anjay_observe_gc() - to determine for which SSIDs to keep observe request
 *   information
 */
int _anjay_servers_foreach_ssid(anjay_t *anjay,
                                anjay_servers_foreach_ssid_handler_t *handler,
                                void *data);

typedef int anjay_servers_foreach_handler_t(anjay_t *anjay,
                                            anjay_server_info_t *server,
                                            void *data);

/**
 * Iterates over ACTIVE servers known to the server subsystem. Active servers
 * are those which have a valid socket created - valid, but not necessarily
 * ready for communication, e.g. in queue mode.
 *
 * The sockets are returned as pointers to anjay_server_info_t, which allows
 * calling more methods on them.
 */
int _anjay_servers_foreach_active(anjay_t *anjay,
                                  anjay_servers_foreach_handler_t *handler,
                                  void *data);

/**
 * Schedules the "servers reload" operation. It can primarily be thought as
 * synchronizing the server connections contained within anjay->servers with the
 * data model, but it also handles some actions that shall be executed when the
 * servers are discovered to have changed state (registration, deregistration,
 * sending outstanding notifications etc.).
 *
 * The job is scheduled immediately and the callback is
 * reload_servers_sched_job(). Its semantics are as follows:
 *
 * 1. For each instance of the LwM2M Security object:
 * 1.1. Read the SSID (and the Bootstrap Server flag, mapping it to 65535)
 * 1.2. Call reload_server_by_ssid(), which does:
 * 1.2.1. If the server has already existed and been active, call
 *        reload_active_server(), which does:
 * 1.2.1.1. If it's not a Bootstrap Server and its registration has expired,
 *          deactivate it (scheduling a reactivation immediately) - see below
 *          for explanation of the activation and deactivation flows
 * 1.2.1.2. Call _anjay_active_server_refresh(). If it fails, deactivate the
 *          server. See below for information about what that function does.
 * 1.2.1.3. If it is a bootstrap server which does not have the "primary"
 *          connection configured yet (for details, see
 *          _anjay_server_primary_connection_valid()) - signifying a freshly
 *          activated (or failed?) instance - call
 *          _anjay_bootstrap_update_reconnected(), which reschedules
 *          Client-Initiated Bootstrap if applicable. Eventually,
 *          request_bootstrap() will call
 *          _anjay_server_setup_primary_connection()
 * 1.2.1.4. If it is a non-bootstrap server which does not have the "primary"
 *          connection configured yet, invalidate the registration (to enforce
 *          Register once everything goes well) and deactivate the server.
 * 1.2.1.5. If it is a non-bootstrap server with proper connectivity, schedule
 *          flush of the notifications (_anjay_observe_sched_flush()).
 * 1.2.2. If the server has already existed, been inactive, is not already
 *        scheduled for reactivation, but has data_inactive.reactivate_time set
 *        - schedule reactivation. See the docs of that field in
 *        anjay_server_info_t for details.
 * 1.2.3. If the server has already existed and been inactive, in any other case
 *        than described in 1.2.2 - consider reload a success.
 * 1.2.4. If we're here, it means that the server has not previously existed.
 *        Create a new inactive server entry and schedule its activation
 *        immediately (unless it's the Bootstrap Server and legacy
 *        Server-Initiated Bootstrap is disabled - Client-Initiated Bootstrap
 *        will be handled later).
 * 2. If stage 1 was a success, and if the result is that we have only one
 *    server in the data model, which is the Bootstrap Server, that is inactive
 *    and not scheduled for activation - schedule its activation immediately.
 *    This compensates for step 1.2.4 for Client-Initiated Bootstrap when legacy
 *    Server-Initiated Bootstrap is disabled.
 * 3. If the reload in stage 1 was unsuccessful (which may happen in the case of
 *    some REALLY FATAL error, such as failure to iterate over the data model or
 *    to schedule a job), retain all the servers that has been successfully
 *    reloaded, move the untouched remainder of servers that existed before
 *    reloading to the current state, and reschedule whole procedure after
 *    5 seconds.
 * 4. Call _anjay_observe_gc() to remove observation entries for servers that
 *    ceased to exist.
 * 5. Call Deregister on all servers that ceased to exist but were previously
 *    active.
 * 6. Clean up.
 *
 * The "server reactivation" procedure is performed within the
 * activate_server_job() function and basically consists of the following:
 *
 * 1. Call initialize_active_server(), which does:
 * 1.1. Fail immediately if we're in offline mode.
 * 1.2. Read the URI from the Security instance.
 * 1.3. Call _anjay_active_server_refresh(), returning a failure (but see notes
 *      below) if it fails
 * 1.4. If it's not a Bootstrap Server, call
 *      _anjay_server_ensure_valid_registration(), which:
 *      - will do nothing if the server has valid registration and there is no
 *        need to send the Update message whatsoever
 *      - will send UPDATE message if the server has valid registration but some
 *        of its details has changed (i.e., the values sent within the Update
 *        message)
 *      - will send REGISTER message if the server has no valid registration
 *        (i.e. it's new or its session has been replaced instead of resumed),
 *        or if the aforementioned attept to send Update failed (Updates
 *        automatically degenerate to Registers within this function, and
 *        internal structures tracking registration state are updated
 *        accordingly) - NOTE THAT THIS IS THE **ONLY** PLACE IN THE ENTIRE CODE
 *        FLOW IN WHICH THE REGISTER MESSAGE MAY BE SENT
 * 1.5. If it is a Bootstrap Server, call _anjay_bootstrap_account_prepare(),
 *      which will schedule Client-Initiated Bootstrap if applicable.
 * 1.6. If the above was a success, reset the reactivate_failed flag, the
 *      num_icmp_failures counter and the reactivate_time value.
 * 2. If stage 1 was unsuccessful:
 * 2.1. Clean up the sockets (essentially deactivating the server).
 * 2.2. If there was an ECONNREFUSED error during _anjay_active_server_refresh()
 *      (which covers DTLS handshake, but NOT the Register/Update messages -
 *      TODO: NEEDS FIXING), increase the num_icmp_failures counter
 * 2.3. If there was a 4.03 Forbidden response to the attempt to send Register
 *      (note that Update is unaffected as it immediately degenerates to
 *      Register) or if _anjay_active_server_refresh() failed due to EPROTO or
 *      ETIMEDOUT (essentially a network failure during DTLS handshake other
 *      than Connection Refused), max out the num_icmp_failures counter, causing
 *      to immediately land in step 2.6.
 * 2.4. If there was any other failure (e.g. some other error response to
 *      Register) - do not touch the num_icmp_failures counter. Such failures
 *      may happen indefinitely.
 * 2.5. If the num_icmp_failures counter is smaller than the limit, retry the
 *      activation job (uses backoff controlled by
 *      _anjay_servers_schedule_{first,next}_retryable()).
 * 2.6. If the ICMP failures limit has been reached:
 * 2.6.1. If we're attempting to activate the Bootstrap Server, abort all
 *        further Client-Initiated Bootstrap attempts.
 * 2.6.2. Otherwise, if there is a Bootstrap Server, there are no active
 *        non-Bootstrap servers, all other servers have reached the ICMP
 *        failures limit and Bootstrap is not already in progress:
 * 2.6.2.1. If the Bootstrap Server is active, call
 *          _anjay_bootstrap_account_prepare(), eventually sending Request
 *          Bootstrap.
 * 2.6.2.2. Otherwise, schedule immediate activation of the Bootstrap Server -
 *          this covers the case when Server-Initiated Bootstrap is disabled and
 *          the Bootstrap Server is not active when not in the Client-Initiated
 *          Bootstrap procedure.
 * 2.6.3. Prevent activate_server_job() from ever being called again, until
 *        anjay_schedule_reconnect() is manually called.
 *
 * Server deactivation is normally handled by _anjay_server_deactivate(). The
 * server might also enter inactive state through enter_offline_job(), or in
 * case of failure in activate_server_job(). Still, _anjay_server_deactivate()
 * works as follows:
 *
 * 1. If the server has a valid registration - send Deregister. Intentionally
 *    ignore errors if it fails for any reason - Deregister is optional anyway.
 * 2. Call _anjay_server_clean_active_data(), which cleans up the sockets and
 *    unschedules any planned jobs, and reset primary_conn_type to UNSET.
 *    However, anjay_server_connection_t::nontransient_state fields are
 *    intentionally NOT cleaned up, and they contain:
 *    - preferred_endpoint, i.e. the preference which server IP address to use
 *      if multiple are returned during DNS resolution
 *    - DTLS session cache
 *    - Last bound local port
 * 3. Schedule reactivation after the specified amount of time.
 */
int _anjay_schedule_reload_servers(anjay_t *anjay);

/**
 * Interrupts any ongoing communication with connections that are
 * administratively set to be offline.
 *
 * Intended to be calling when entering offline mode, to prevent data being sent
 * between scheduler ticks.
 */
void _anjay_servers_interrupt_offline(anjay_t *anjay);

////////////////////////////////////////////////////////////////////////////////
// METHODS ON ACTIVE SERVERS ///////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/**
 * Gets the SSID of the server in question.
 */
anjay_ssid_t _anjay_server_ssid(anjay_server_info_t *server);

anjay_iid_t _anjay_server_last_used_security_iid(anjay_server_info_t *server);

anjay_t *_anjay_from_server(anjay_server_info_t *server);

/**
 * Gets the administratively configured binding mode of the server in question.
 */
const anjay_binding_mode_t *
_anjay_server_binding_mode(anjay_server_info_t *server);

/**
 * Gets the token uniquely identifying the CoAP endpoint association (i.e., DTLS
 * session or raw UDP socket) of the server's primary connection.
 *
 * It is used to determine whether reconnect operation re-used the previous
 * association or created a new one.
 */
anjay_conn_session_token_t
_anjay_server_primary_session_token(anjay_server_info_t *server);

/**
 * Gets the information about current registration status of the server. These
 * include the data sent within the Update method's payload, and also the
 * endpoint path and the expiration time (the point in time at which the
 * registration lifetime passes).
 */
const anjay_registration_info_t *
_anjay_server_registration_info(anjay_server_info_t *server);

/**
 * Updates the registration information (the same that can be queried through
 * _anjay_server_registration_info()) within the server. The endpoint path and
 * Update parameters can be updated directly through the arguments, and the
 * expiration time is calculated by adding move_params->lifetime_s seconds to
 * the RTC reading at the time of calling this function.
 *
 * NOTE: If any of the move_* parameters are NULL, the relevant fields are NOT
 * updated, i.e., they are left untouched rather than being replaced with NULLs.
 *
 * This is called from _anjay_register() and _anjay_update_registration() to
 * update the internally stored values with actual negotiated data, and from
 * _anjay_schedule_socket_update() to invalidate registration.
 */
void _anjay_server_update_registration_info(
        anjay_server_info_t *server,
        AVS_LIST(const anjay_string_t) *move_endpoint_path,
        anjay_lwm2m_version_t lwm2m_version,
        bool queue_mode,
        anjay_update_parameters_t *move_params);

/**
 * Handles a critical error (including network communication error) on the
 * primary connection of the server. Effectively disables the server, and might
 * schedule Client-Initiated Bootstrap if applicable.
 */
void _anjay_server_on_failure(anjay_server_info_t *server,
                              const char *debug_msg);

/** Calls @ref _anjay_server_on_failure() through the scheduler. */
void _anjay_server_on_server_communication_error(anjay_server_info_t *server,
                                                 avs_error_t err);

/**
 * Handles a network timeout during Registration (or Request Bootstrap) on the
 * primary connection of the server - this retries connection if the connection
 * was stable (i.e. not just freshly connected and not stateless), or calls
 * @ref _anjay_server_on_server_communication_error otherwise.
 */
void _anjay_server_on_server_communication_timeout(anjay_server_info_t *server);

void _anjay_server_on_fatal_coap_error(anjay_connection_ref_t conn_ref);

////////////////////////////////////////////////////////////////////////////////
// METHODS ON SERVER CONNECTIONS ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/**
 * Returns the CACHED URI of the given connection - the one that was most
 * recently read in initialize_active_server().
 *
 * It is called from send_request_bootstrap() and send_register() to fill the
 * Uri-Path option in the outgoing messages.
 */
const anjay_url_t *_anjay_connection_uri(anjay_connection_ref_t ref);

/**
 * This function is called from _anjay_release_connection() - if the
 * connection is in queue mode, it schedules closing of the socket (suspending
 * the connection) after MAX_TRANSMIT_WAIT passes.
 */
void _anjay_connection_schedule_queue_mode_close(anjay_connection_ref_t ref);

/**
 * Returns the socket associated with a given connection, if it exists and is in
 * online state, ready for communication.
 *
 * It's used in _anjay_bind_connection(), get_online_connection_socket()
 * and find_by_udp_socket_clb() to actually get the sockets, but also in various
 * places to just check whether the connection is online.
 */
avs_net_socket_t *
_anjay_connection_get_online_socket(anjay_connection_ref_t ref);

bool _anjay_connection_ready_for_outgoing_message(anjay_connection_ref_t ref);

/**
 * This function only makes sense when the connection is online. It marks the
 * connection as "stable", which makes it eligible for reconnection if
 * communication error occurs from now on.
 *
 * It is called from _anjay_server_on_refreshed().
 */
void _anjay_connection_mark_stable(anjay_connection_ref_t ref);

/**
 * This function only makes sense when the connection is in a suspended (active
 * but not online) state. It rebinds and reconnects the socket. Data model is
 * not queried, as all information (hostname, ports, DTLS security information)
 * is retrieved from the pre-existing socket.
 *
 * It is called from observe_core.c :: ensure_conn_online().
 */
void _anjay_connection_bring_online(anjay_connection_ref_t ref);

/**
 * Suspends the specified connection (or all connections in the server if
 * conn_ref.conn_type == ANJAY_CONNECTION_UNSET). Suspending the connection
 * means closing the socket, but not cleaning it up. The connection (and server)
 * is then still considered active, but not online.
 */
void _anjay_connection_suspend(anjay_connection_ref_t conn_ref);

anjay_socket_transport_t
_anjay_connection_transport(anjay_connection_ref_t conn_ref);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_PRIVATE_H
