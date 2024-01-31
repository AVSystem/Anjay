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

/**
 * @return The number of milliseconds that have elapsed since the system was
 * started
 */
uint64_t anj_time_now(void);

#endif // ANJ_TIME_H
