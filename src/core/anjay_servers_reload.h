/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_SERVERS_RELOAD_H
#define ANJAY_SERVERS_RELOAD_H

#include <anjay_modules/anjay_servers.h>

#if !(defined(ANJAY_SERVERS_INTERNALS) || defined(ANJAY_LWM2M_SEND_SOURCE) \
      || defined(ANJAY_TEST))
#    error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_schedule_delayed_reload_servers(anjay_unlocked_t *anjay);

int _anjay_schedule_refresh_server(anjay_server_info_t *server,
                                   avs_time_duration_t delay);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_RELOAD_H
