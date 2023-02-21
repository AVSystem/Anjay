/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_SERVERS_INACTIVE_H
#define ANJAY_SERVERS_INACTIVE_H

#include "anjay_servers_private.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_OBSERVE_SOURCE) \
        && !defined(ANJAY_TEST)
#    error "servers_inactive.h shall only be included from " \
           "servers/ or from the Observe subsystem"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

anjay_server_info_t *_anjay_servers_find(anjay_unlocked_t *anjay,
                                         anjay_ssid_t ssid);

bool _anjay_server_is_disable_scheduled(anjay_server_info_t *server);

bool _anjay_server_active(anjay_server_info_t *server);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_INACTIVE_H
