/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
anjay_server_info_t *_anjay_servers_find_active(anjay_unlocked_t *anjay,
                                                anjay_ssid_t ssid);

anjay_server_info_t *
_anjay_servers_find_active_by_security_iid(anjay_unlocked_t *anjay,
                                           anjay_iid_t security_iid);

anjay_connection_ref_t
_anjay_servers_find_active_primary_connection(anjay_unlocked_t *anjay,
                                              anjay_ssid_t ssid);

/**
 * @returns Point in time at which the server registration expires.
 */
avs_time_real_t _anjay_registration_expire_time(anjay_server_info_t *server);

bool _anjay_server_registration_expired(anjay_server_info_t *server);

int _anjay_schedule_socket_update(anjay_unlocked_t *anjay,
                                  anjay_iid_t security_iid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_H
