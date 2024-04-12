/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJ_TIME_H
#define ANJ_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @return The number of milliseconds that have elapsed since the system was
 * started.
 */
uint64_t anj_time_now(void);

/**
 * @return The current system Unix time expressed in milliseconds.
 */
uint64_t anj_time_real_now(void);

#ifdef __cplusplus
}
#endif

#endif // ANJ_TIME_H
