/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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

typedef struct {
    avs_coap_exchange_id_t exchange_id;
    anjay_lwm2m_version_t attempted_version;
    anjay_update_parameters_t new_params;
} anjay_registration_async_exchange_state_t;

typedef enum {
    /**
     * Handles connectivity failures, which involves scheduling reconnection,
     * etc. Scheduled by _anjay_server_on_server_communication_error(), which
     * is called in a number of error handling paths.
     */
    ANJAY_SERVER_NEXT_ACTION_COMMUNICATION_ERROR,

    /**
     * Disables the server and schedules its reactivation after the delay
     * specified by the /1/x/5 resource. Scheduled by the anjay_disable_server()
     * public API.
     */
    ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_TIMEOUT_FROM_DM,

    /**
     * Disables the server and schedules its reactivation after the delay
     * specified by the anjay_server_info_t::reactivate_time field. Scheduled
     * by the _anjay_schedule_disable_server_with_explicit_timeout_unlocked()
     * API.
     */
    ANJAY_SERVER_NEXT_ACTION_DISABLE_WITH_EXPLICIT_TIMEOUT,

    /**
     * Updates the registration. Makes sense only for active servers. Scheduled
     * either immediately (normally via anjay_schedule_registration_update()),
     * when Update is forced, or delayed by "lifetime minus eta", scheduled
     * after a successful Register or Update operation.
     */
    ANJAY_SERVER_NEXT_ACTION_SEND_UPDATE,

    /**
     * Scheduled from _anjay_schedule_refresh_server(), calls
     * _anjay_active_server_refresh(). Used in many places, including
     * _anjay_server_sched_activate(), _anjay_schedule_reload_servers()
     * _anjay_schedule_registration_update_unlocked(), as well as in
     * start_send_exchange() (to force getting out of the queue mode, if
     * applicable). See the code and documentation for those functions for
     * details.
     */
    ANJAY_SERVER_NEXT_ACTION_REFRESH
} anjay_server_next_action_t;

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
    anjay_unlocked_t *anjay;

    anjay_ssid_t ssid; // or ANJAY_SSID_BOOTSTRAP

    anjay_iid_t last_used_security_iid;

    /**
     * Scheduler jobs that shall be executed for the given server are scheduled
     * using this handle. The specific action to perform is controlled by the
     * <c>next_action</c> field.
     */
    avs_sched_handle_t next_action_handle;

    /**
     * Action to be performed by the job scheduled in <c>next_action_handle</c>.
     * See @ref anjay_server_next_action_t for specific actions.
     */
    anjay_server_next_action_t next_action;

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
     * Specifies the time at which the reactivate job shall be executed.
     *
     * If Anjay enters offline mode, we delete all such jobs (because we don't
     * want servers to be activated during offline mode) - but thanks to this
     * value, we can reschedule activation at appropriate time even after
     * exiting offline mode.
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

#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    /**
     * Stores the time when the last communication with a given server was done.
     * Note that some messages don't get any confirmation from the server so the
     * point in time this variable holds is an approximation.
     */
    avs_time_real_t last_communication_time;
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
};

#ifndef ANJAY_WITHOUT_DEREGISTER
void _anjay_servers_internal_deregister(AVS_LIST(anjay_server_info_t) *servers);
#else // ANJAY_WITHOUT_DEREGISTER
#    define _anjay_servers_internal_deregister(Servers) ((void) (Servers))
#endif // ANJAY_WITHOUT_DEREGISTER

void _anjay_servers_internal_cleanup(AVS_LIST(anjay_server_info_t) *servers);

void _anjay_server_clean_active_data(anjay_server_info_t *server);

/**
 * Cleans up server data. Does not send De-Register message.
 */
void _anjay_server_cleanup(anjay_server_info_t *server);

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_insert_ptr(AVS_LIST(anjay_server_info_t) *servers,
                               anjay_ssid_t ssid);

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_ptr(AVS_LIST(anjay_server_info_t) *servers,
                        anjay_ssid_t ssid);

int _anjay_server_reschedule_next_action(
        anjay_server_info_t *server,
        avs_time_duration_t delay,
        anjay_server_next_action_t next_action);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SERVERS_H
