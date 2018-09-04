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

#ifndef ANJAY_SERVERS_SERVERS_H
#define ANJAY_SERVERS_SERVERS_H

#include <anjay/core.h>

#include "../servers.h"

#include "connection_info.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_TEST)
#error "Headers from servers/ are not meant to be included from outside"
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
    anjay_ssid_t ssid; // or ANJAY_SSID_BOOTSTRAP

    /**
     * Scheduler jobs that shall be executed for the given server are scheduled
     * using this handle. There are currently three actions possible:
     *
     * - activate_server_job() - server reactivation. Makes sense only for
     *   inactive servers. Scheduled either immediately, if we are explicitly
     *   attempting to connect to the server, or with a delay (scheduled from
     *   _anjay_server_deactivate()) when time-limited deactivation is ordered.
     * - send_update_sched_job() - updating the registration. Makes sense only
     *   for active servers. Scheduled either immediately (normally via
     *   anjay_schedule_registration_update()), when Update is forced, or
     *   delayed by "lifetime minus eta", scheduled after a successful Register
     *   or Update operation.
     * - reload_server_by_ssid_job() - reloading the server without deactivating
     *   it. It is only ever used during the _anjay_schedule_server_reconnect()
     *   execution path, so see the docs there for details.
     */
    anjay_sched_handle_t next_action_handle;

    /**
     * When the "classic" server subsystem backoff mechanism (the
     * _anjay_servers_schedule_{first,next}_retryable() functions) is in use,
     * this fields contain the delay to use for the next retry of the action
     * currently scheduled in next_action_handle.
     *
     * That mechanism is currently in use by all the three functions mentioned
     * above in docs for next_action_handle.
     */
    avs_time_duration_t next_retry_delay;

    /**
     * The fields in data_active substruct are valid only for active servers...
     * except they are not. They are also used for determining whether the
     * server is active or not (as sockets are stored within
     * anjay_server_connection_t objects, see the main docstring for
     * anjay_server_info_t for details), and also holds non-transient data that
     * is of no use when the server is inactive, but is preserved between
     * activation attempts (so that session resumption works across
     * activations).
     */
    struct {
        /**
         * Cached URI of the given server - this is exactly the value returned
         * by _anjay_server_uri().
         */
        anjay_url_t uri;

        /**
         * Connection (socket, binding) entries - see docs to
         * anjay_server_connection_t in connection_info.h for details.
         */
        anjay_server_connection_t udp_connection;

        /**
         * Information about which connection is currently the "primary" one.
         * The "primary" connection is the one on which the autonomous outgoing
         * messages (i.e. Register/Update or Bootstrap Request) are sent. See
         * the docs in server.h for details (Ctrl+F the word "primary").
         */
        anjay_connection_type_t primary_conn_type;

        /**
         * Information about current registration status of the server. See the
         * docs for _anjay_server_registration_info() and
         * _anjay_server_update_registration_info() for details.
         */
        anjay_registration_info_t registration_info;
    } data_active;

    /**
     * The fields in data_inactive substruct are valid only for inactive servers
     */
    struct {
        /**
         * When a reactivate job is scheduled (and its handle stored in
         * next_action_handle), this field is filled with the time for which the
         * reactivate job is (initially) scheduled. If Anjay enters offline
         * mode, we delete all such jobs (because we don't want servers to be
         * activated during offline mode) - but thanks to this value, we can
         * reschedule activation at appropriate time after exiting offline mode.
         *
         * This logic has been first introduced in internal diff D7056, which
         * limited the number of places in code where Registers and Updates may
         * happen, to deliver more consistent behaviour of those. Previously,
         * enter_offline_job() did not completely deactivate the servers, but
         * just suspended (closed) their sockets, and
         * _anjay_server_ensure_valid_registration() was called directly from
         * reload_active_server() (as the servers exiting from offline modes
         * were considered active). This yielded inconsistent behaviour of
         * Update error handling - Updates generated in this way were not
         * degenerating to Registers immediately.
         */
        avs_time_real_t reactivate_time;

        /**
         * True if, and only if, the last activation attempt was unsuccessful,
         * for whatever reason - not necessarily those included in
         * num_icmp_failures logic.
         */
        bool reactivate_failed;

        /**
         * Counter that is increased in case of some kind of ICMP Unreachable
         * message received while trying to communicate with the server.
         *
         * Its value also skips to anjay->max_icmp_failures in case of a 4.03
         * Forbidden CoAP response to Register, a network timeout, or a DTLS
         * handshake error.
         */
        uint32_t num_icmp_failures;
    } data_inactive;
};

/**
 * Schedules a job with "classic" backoff mechanism. The job is scheduled into
 * the next_action_handle field, the information necessary to manage the backoff
 * is stored in next_retry_delay, and the job can reschedule itself using
 * _anjay_servers_schedule_next_retryable().
 *
 * This mechanism is DEPRECATED and shall not be used in new code, due to it
 * being unpredictable and uncontrollable. New code is encouraged to use retry
 * mechanisms based on CoAP transmission parameters.
 */
int _anjay_servers_schedule_first_retryable(anjay_sched_t *sched,
                                            anjay_server_info_t *server,
                                            avs_time_duration_t delay,
                                            anjay_sched_clb_t clb,
                                            anjay_ssid_t ssid);

/**
 * Reschedules a job previously scheduled using
 * _anjay_servers_schedule_first_retryable(). The idea is that the job will call
 * this function itself when it wants to retry.
 *
 * The first retry is scheduled with a 1 second long delay, and each next delay
 * will be twice as long, until reaching 2 minutes - at which point each next
 * delay will be exactly 2 minutes long.
 */
int _anjay_servers_schedule_next_retryable(anjay_sched_t *sched,
                                           anjay_server_info_t *server,
                                           anjay_sched_clb_t clb,
                                           anjay_ssid_t ssid);

void _anjay_servers_internal_deregister(anjay_t *anjay,
                                        anjay_servers_t *servers);

void _anjay_servers_internal_cleanup(anjay_t *anjay,
                                     anjay_servers_t *servers);

void _anjay_server_clean_active_data(const anjay_t *anjay,
                                     anjay_server_info_t *server);

/**
 * Cleans up server data. Does not send De-Register message.
 */
void _anjay_server_cleanup(const anjay_t *anjay, anjay_server_info_t *server);

bool _anjay_server_active(anjay_server_info_t *server);

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_insert_ptr(anjay_servers_t *servers, anjay_ssid_t ssid);

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_ptr(anjay_servers_t *servers, anjay_ssid_t ssid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SERVERS_H
