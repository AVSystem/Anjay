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

#ifndef ANJAY_SERVERS_UTILS_H
#define ANJAY_SERVERS_UTILS_H

#include "servers.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Returns an active server object associated with given @p socket .
 */
anjay_server_info_t *
_anjay_servers_find_by_udp_socket(anjay_t *anjay,
                                  avs_net_abstract_socket_t *socket);

/**
 * Returns a server object for given SSID.
 *
 * NOTE: the bootstrap server is identified by the ANJAY_SSID_BOOTSTRAP
 * constant instead of its actual SSID.
 */
anjay_server_info_t *_anjay_servers_find_active(anjay_t *anjay,
                                                anjay_ssid_t ssid);

bool _anjay_server_registration_expired(anjay_server_info_t *server);

int _anjay_schedule_socket_update(anjay_t *anjay, anjay_iid_t security_iid);

/**
 * Determines the connection mode (offline, online or queue-mode) for a specific
 * connection type appropriate for a given binding mode.
 */
anjay_server_connection_mode_t
_anjay_get_connection_mode(const char *binding_mode,
                           anjay_connection_type_t conn_type);

/**
 * Gets the current ACTUAL Binding mode of the given server - the one that is
 * actually in effect.
 *
 * For example, if the binding mode is nominally configured in the data model to
 * be US, but the UDP connection failed and is not available - "S"
 * is returned.
 */
void _anjay_server_actual_binding_mode(anjay_binding_mode_t *out_binding_mode,
                                       anjay_server_info_t *server);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_H
