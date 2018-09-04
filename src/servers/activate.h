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

#ifndef ANJAY_SERVERS_ACTIVATE_H
#define ANJAY_SERVERS_ACTIVATE_H

#include <anjay_modules/time_defs.h>

#include "../anjay_core.h"
#include "../utils_core.h"

#ifndef ANJAY_SERVERS_INTERNALS
#error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Schedules a @ref _anjay_server_activate execution after given @p delay.
 *
 * Activation is performed as a retryable job, so it does not need to be
 * repeated by the caller.
 *
 * After the activation succeeds, the scheduled job takes care of any required
 * Registration Updates.
 */
int _anjay_server_sched_activate(anjay_t *anjay,
                                 anjay_server_info_t *server,
                                 avs_time_duration_t reactivate_delay);

int _anjay_servers_sched_reactivate_all_given_up(anjay_t *anjay);

/**
 * Inserts an active server entry into @p servers .
 *
 * This function is meant to be used only for initialization of the @p servers
 * object, which should NOT contain any server entry with the same SSID as
 * @p server .
 *
 * Does not modify scheduled update job for @p server.
 */
void _anjay_servers_add(anjay_servers_t *servers,
                        AVS_LIST(anjay_server_info_t) server);
/**
 * Deactivates the active server entry associated with @p ssid . Fails if there
 * is no active server entry with such @p ssid .
 *
 * If @p reactivate_delay is not AVS_TIME_DURATION_INVALID, schedules a
 * reactivate job after @p reactivate_delay. The job is a retryable one, so
 * the caller does not need to worry about reactivating the server manually.
 */
int _anjay_server_deactivate(anjay_t *anjay,
                             anjay_ssid_t ssid,
                             avs_time_duration_t reactivate_delay);

/**
 * Creates a new detached inactive server entry for given @p ssid .
 *
 * Does not schedule the reactivate job for created entry.
 */
AVS_LIST(anjay_server_info_t) _anjay_servers_create_inactive(anjay_ssid_t ssid);

/**
 * Checks whether now is a right moment to initiate Client Initiated Bootstrap
 * as per requirements in the specification.
 *
 * @returns true if all requirements for Client Initiated Bootstrap are met,
 *          false otherwise.
 */
bool _anjay_should_retry_bootstrap(anjay_t *anjay);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_ACTIVATE_H
