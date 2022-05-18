/*
 * Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H
#define ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H

#include <stdbool.h>

#include <anjay_modules/anjay_io_utils.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_BOOTSTRAP

bool _anjay_bootstrap_in_progress(anjay_unlocked_t *anjay);

#    ifdef ANJAY_WITH_LWM2M11
int _anjay_schedule_bootstrap_request_unlocked(anjay_unlocked_t *anjay);
#    endif // ANJAY_WITH_LWM2M11

#else

#    define _anjay_bootstrap_in_progress(anjay) ((void) (anjay), false)

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H */
