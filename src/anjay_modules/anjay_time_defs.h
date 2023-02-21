/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_TIME_H
#define ANJAY_INCLUDE_ANJAY_MODULES_TIME_H

#include <assert.h>
#include <stdint.h>

#include <avsystem/commons/avs_time.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define NUM_NANOSECONDS_IN_A_SECOND (1L * 1000L * 1000L * 1000L)
#define NUM_SECONDS_IN_A_DAY 86400

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_TIME_H */
