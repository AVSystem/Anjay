/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_DM_QUERY_H
#define ANJAY_DM_QUERY_H

#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_find_server_iid(anjay_unlocked_t *anjay,
                           anjay_ssid_t ssid,
                           anjay_iid_t *out_iid);

int _anjay_ssid_from_server_iid(anjay_unlocked_t *anjay,
                                anjay_iid_t server_iid,
                                anjay_ssid_t *out_ssid);

#ifdef ANJAY_WITH_BOOTSTRAP
bool _anjay_is_bootstrap_security_instance(anjay_unlocked_t *anjay,
                                           anjay_iid_t security_iid);

anjay_iid_t _anjay_find_bootstrap_security_iid(anjay_unlocked_t *anjay);
#else
#    define _anjay_is_bootstrap_security_instance(...) (false)
#    define _anjay_find_bootstrap_security_iid(...) ANJAY_ID_INVALID
#endif

avs_time_duration_t
_anjay_disable_timeout_from_server_iid(anjay_unlocked_t *anjay,
                                       anjay_iid_t server_iid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_DM_QUERY_H
