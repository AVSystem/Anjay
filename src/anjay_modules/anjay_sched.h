/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_SCHED_H
#define ANJAY_INCLUDE_ANJAY_MODULES_SCHED_H

#include <avsystem/commons/avs_sched.h>

#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

static inline anjay_t *_anjay_get_from_sched(avs_sched_t *sched) {
    return (anjay_t *) avs_sched_data(sched);
}

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_SCHED_H */
