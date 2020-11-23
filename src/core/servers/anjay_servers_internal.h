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

#ifndef ANJAY_SERVERS_SERVERS_H
#define ANJAY_SERVERS_SERVERS_H

#include <anjay/core.h>

#include "../anjay_servers_private.h"

#include "anjay_connections.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_TEST)
#    error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * This structure holds information about server connections.
 */
struct anjay_servers_struct {
    /**
     * List of known LwM2M servers we may want to be connected to. This is
     * semantically a map, keyed (and ordered) by SSID.
     */
    AVS_LIST(anjay_server_info_t) servers;

    /**
     * Cache of anjay_socket_entry_t objects, returned by
     * anjay_get_socket_entries(). These entries are never used for anything
     * inside the library, it's just to allow returning a list from a function
     * without requiring the user to clean it up.
     */
    AVS_LIST(anjay_socket_entry_t) public_sockets;
};

typedef struct {
    avs_coap_exchange_id_t exchange_id;
    anjay_lwm2m_version_t attempted_version;
    anjay_update_parameters_t new_params;
} anjay_registration_async_exchange_state_t;

/**
 * Information about a known LwM2M server.
 *
 * The server may be considered "active" or "inactive". A server is "active" if
 * it has any socket created - not necessarily connected an online, but created.
 * The active state is normal for servers. Here are the circumstances in which
 * inactive server entries may exist:
 *
 * - Freshly after creation - all server entries are created in the inactive
 *   state, and activated afterwards.
 * - After activation failure - if e.g. there was an error connecting the
 *   socket.
 * - Administratively disabled - one may call anjay_disable_server() or
 *   anjay_disable_server_with_timeout(); this shall normally done only in
 *   reaction to an Execute operation on the Disable resource in the Server
 *   object.
 * - When Re-Registration to the server is necessary - it will be deactivated
 *   and activated again for Registration, as initialize_active_server() is the
 *   only place in the codebase that may order sending Register message.
 * - When the library is ordered to enter into Offline mode using
 *   anjay_enter_offline() - all servers are deactivated then.
 *
 * See documentation to _anjay_schedule_reload_servers() for details on
 * activation and deactivation flow.
 */
struct anjay_server_info_struct {
    anjay_t *anjay;

    anjay_ssid_t ssid; // or ANJAY_SSID_BOOTSTRAP

    anjay_iid_t last_used_security_iid;

    /**
     * Scheduler jobs that shall be executed for the given server are scheduled
     * using this handle. There are currently three actions possible:
     *
     * - refresh_server_job() - calls _anjay_schedule_refresh_server(). Used
     *   during the _anjay_schedule_server_reconnect() execution path (see the
     *   docs there for details), as well as for reactivating servers - either
     *   immediately, if we are explicitly attempting to connect to the server,
     *   or with a delay (scheduled from _anjay_server_deactivate()) when
     *   time-limited deactivation is ordered.
     * - send_update_sched_job() - updating the registration. Makes sense only
     *   for active servers. Scheduled either immediately (normally via
     *   anjay_schedule_registration_update()), when Update is forced, or
     *   delayed by "lifetime minus eta", scheduled after a successful Register
     *   or Update operation.
     */
    avs_sched_handle_t next_action_handle;

    /**
     * Administratively configured binding mode, cached from the data model.
     */
    anjay_binding_mode_t binding_mode;

    /**
     * State of all connections to remote servers possible for a given server.
     * The anjay_connections_t type wraps the actual server connections,
     * information about which is currently the "primary" one, and manages the
     * connection state flow.
     *
     * This object is also used for determining whether the server is active or
     * not (as sockets are stored inside, see the main docstring for
     * anjay_server_info_t for details), and also holds non-transient data that
     * is of no use when the server is inactive, but is preserved between
     * activation attempts (so that session resumption works across
     * activations).
     */
    anjay_connections_t connections;

    /**
     * Information about current registration status of the server. See the
     * docs for _anjay_server_registration_info() and
     * _anjay_server_update_registration_info() for details.
     */
    anjay_registration_info_t registration_info;

    anjay_registration_async_exchange_state_t registration_exchange_state;

    /**
     * When a reactivate job is scheduled (and its handle stored in
     * next_action_handle), this field is filled with the time for which the
     * reactivate job is (initially) scheduled. If Anjay enters offline mode, we
     * delete all such jobs (because we don't want servers to be activated
     * during offline mode) - but thanks to this value, we can reschedule
     * activation at appropriate time after exiting offline mode.
     *
     * This logic has been first introduced in internal diff D7056, which
     * limited the number of places in code where Registers and Updates may
     * happen, to deliver more consistent behaviour of those. Previously,
     * enter_offline_job() did not completely deactivate the servers, but just
     * suspended (closed) their sockets, and
     * _anjay_server_ensure_valid_registration() was called directly from
     * reload_active_server() (as the servers exiting from offline modes were
     * considered active). This yielded inconsistent behaviour of Update error
     * handling - Updates generated in this way were not degenerating to
     * Registers immediately.
     */
    avs_time_real_t reactivate_time;

    /**
     * True if, and only if, the last activation attempt was unsuccessful, for
     * whatever reason - not necessarily those included in num_icmp_failures
     * logic.
     */
    bool refresh_failed;

    /**
     * Number of attempted (potentially) failed registrations. It is incremented
     * in send_register(), then compared (if non-zero) against "Communication
     * Retry Count" resource in _anjay_server_on_failure(). When the
     * registration succeeds, it is reset to 0.
     */
    uint32_t registration_attempts;

    /**
     * Number of completely performed Communication Retry Sequences.
     */
    uint32_t registration_sequences_performed;
};

#ifndef ANJAY_WITHOUT_DEREGISTER
void _anjay_servers_internal_deregister(anjay_servers_t *servers);
#else // ANJAY_WITHOUT_DEREGISTER
#    define _anjay_servers_internal_deregister(Servers) ((void) (Servers))
#endif // ANJAY_WITHOUT_DEREGISTER

void _anjay_servers_internal_cleanup(anjay_servers_t *servers);

void _anjay_server_clean_active_data(anjay_server_info_t *server);

/**
 * Cleans up server data. Does not send De-Register message.
 */
void _anjay_server_cleanup(anjay_server_info_t *server);

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_insert_ptr(anjay_servers_t *servers, anjay_ssid_t ssid);

AVS_LIST(anjay_server_info_t) *_anjay_servers_find_ptr(anjay_servers_t *servers,
                                                       anjay_ssid_t ssid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SERVERS_H
