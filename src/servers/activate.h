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

#ifndef ANJAY_SERVERS_ACTIVATE_H
#define	ANJAY_SERVERS_ACTIVATE_H

#include <anjay_modules/time.h>

#include "../anjay.h"
#include "../utils.h"

#ifndef ANJAY_SERVERS_INTERNALS
#error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Schedules a @ref _anjay_server_activate execution after given @p delay.
 * Does nothing if there is no inactive server with given @p ssid .
 *
 * Activation is performed as a retryable job, so it does not need to be
 * repeated by the caller.
 *
 * After the activation succeeds, the scheduled job takes care of any required
 * Registration Updates.
 */
int _anjay_server_sched_activate(anjay_t *anjay,
                                 anjay_servers_t *servers,
                                 anjay_ssid_t ssid,
                                 struct timespec delay);

/**
 * Inserts an active server entry into @p servers .
 *
 * This function is meant to be used only for initialization of the @p servers
 * object, which should NOT contain any server entry with the same SSID as
 * @p server .
 *
 * Does not modify scheduled update job for @p server.
 */
void _anjay_servers_add_active(anjay_servers_t *servers,
                               AVS_LIST(anjay_active_server_info_t) server);
/**
 * Removes active server entry associated with @p ssid and creates a new
 * inactive server entry. Fails if there is no active server entry with such
 * @p ssid .
 *
 * Schedules an reactivate job after @p reactivate delay. The job is a
 * retryable one, so the caller does not need to worry about reactivating the
 * server manually.
 */
anjay_inactive_server_info_t *
_anjay_server_deactivate(anjay_t *anjay,
                         anjay_servers_t *servers,
                         anjay_ssid_t ssid,
                         struct timespec reactivate_delay);

/**
 * Creates a new detached inactive server entry for given @p ssid .
 *
 * Does not schedule the reactivate job for created entry.
 */
AVS_LIST(anjay_inactive_server_info_t)
_anjay_servers_create_inactive(anjay_ssid_t ssid);

/**
 * Inserts an inactive server entry into @p servers.
 *
 * This function is meant to be used only for initialization of the @p servers
 * object, which should NOT contain any server entry with the same SSID as
 * @p server .
 *
 * Does not modify the reactivate job associated with @p server.
 */
void _anjay_servers_add_inactive(anjay_servers_t *servers,
                                 AVS_LIST(anjay_inactive_server_info_t) server);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_ACTIVATE_H
