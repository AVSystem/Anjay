/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_SERVERS_ACTIVATE_H
#define ANJAY_SERVERS_ACTIVATE_H

#include <anjay_modules/anjay_time_defs.h>

#include "anjay_connections.h"
#include "anjay_register.h"

#include "../anjay_core.h"
#include "../anjay_utils_private.h"

#ifndef ANJAY_SERVERS_INTERNALS
#    error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * This function is called as a "callback" whenever
 * @ref _anjay_active_server_refresh finishes its operation.
 *
 * @param server Server object for which the refresh was performed
 *
 * @param state  State of the server's primary connection after refresh
 *
 * @param err    If @p state is @ref ANJAY_SERVER_CONNECTION_ERROR, it shall be
 *               set to the reason of the failure. Otherwise, it shall be
 *               @ref AVS_OK. Currently it is only used to update the "TLS/DTLS
 *               Alert Code" resource if applicable.
 *
 * It performs any operations necessary after the refresh. In particular:
 *
 * - In case of error, refresh_failed flag is updated and retry of either server
 *   refresh or Client-Initiated Bootstrap is scheduled as appropriate.
 * - In case of success on a non-Bootstrap server, the valid registration state
 *   is asserted - Register or Update messages are sent and handled as
 *   necessary.
 * - In case of success on the Bootstrap server, Client-Initiated Bootstrap is
 *   scheduled to be performed if necessary.
 */
void _anjay_server_on_refreshed(anjay_server_info_t *server,
                                anjay_server_connection_state_t state,
                                avs_error_t err);

void _anjay_server_on_updated_registration(anjay_server_info_t *server,
                                           anjay_registration_result_t result,
                                           avs_error_t err);

/**
 * Schedules a @ref _anjay_server_activate execution on
 * <c>server->reactivate_time</c>. Set that field before calling this function
 * to specify the intended reactivation time.
 *
 * Activation is performed as a retryable job, so it does not need to be
 * repeated by the caller.
 *
 * After the activation succeeds, the scheduled job takes care of any required
 * Registration Updates.
 */
int _anjay_server_sched_activate(anjay_server_info_t *server);

int _anjay_servers_sched_reactivate_all_given_up(anjay_unlocked_t *anjay);

/**
 * Inserts an active server entry into @p servers .
 *
 * This function is meant to be used only for initialization of the @p servers
 * object, which should NOT contain any server entry with the same SSID as
 * @p server .
 *
 * Does not modify scheduled update job for @p server.
 */
void _anjay_servers_add(AVS_LIST(anjay_server_info_t) *servers,
                        AVS_LIST(anjay_server_info_t) server);

/**
 * Creates a new detached inactive server entry for given @p ssid .
 *
 * Does not schedule the reactivate job for created entry.
 */
AVS_LIST(anjay_server_info_t)
_anjay_servers_create_inactive(anjay_unlocked_t *anjay, anjay_ssid_t ssid);

/**
 * Synchronous part of @ref anjay_disable_server - this function does what that
 * public API schedules to be executed in an async job.
 */
void _anjay_disable_server_with_timeout_from_dm_sync(
        anjay_server_info_t *server);

/**
 * Synchronous part of
 * @ref _anjay_schedule_disable_server_with_explicit_timeout_unlocked - this
 * function does what that API schedules to be executed in an async job.
 */
void _anjay_disable_server_with_explicit_timeout_sync(
        anjay_server_info_t *server);

/**
 * Checks whether now is a right moment to initiate Client Initiated Bootstrap
 * as per requirements in the specification.
 *
 * @returns true if all requirements for Client Initiated Bootstrap are met,
 *          false otherwise.
 */
anjay_bootstrap_action_t
_anjay_requested_bootstrap_action(anjay_unlocked_t *anjay);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_ACTIVATE_H
