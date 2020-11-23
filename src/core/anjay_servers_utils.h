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

#ifndef ANJAY_SERVERS_UTILS_H
#define ANJAY_SERVERS_UTILS_H

#include "anjay_servers_private.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Returns a server object for given SSID.
 *
 * NOTE: the bootstrap server is identified by the ANJAY_SSID_BOOTSTRAP
 * constant instead of its actual SSID.
 */
anjay_server_info_t *_anjay_servers_find_active(anjay_t *anjay,
                                                anjay_ssid_t ssid);

anjay_server_info_t *
_anjay_servers_find_active_by_security_iid(anjay_t *anjay,
                                           anjay_iid_t security_iid);

anjay_connection_ref_t
_anjay_servers_find_active_primary_connection(anjay_t *anjay,
                                              anjay_ssid_t ssid);

/**
 * @returns Amount of time from now until the server registration expires.
 */
avs_time_duration_t
_anjay_register_time_remaining(const anjay_registration_info_t *info);

bool _anjay_server_registration_expired(anjay_server_info_t *server);

int _anjay_schedule_socket_update(anjay_t *anjay, anjay_iid_t security_iid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_H
