/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#define _GNU_SOURCE

#include <anj/anj_config.h>
#include <anj/anj_time.h>

#if ANJ_TIME_POSIX_COMPAT

#    include <time.h>

uint64_t anj_time_now(void) {
    struct timespec res;
    if (clock_gettime(CLOCK_MONOTONIC, &res)) {
        return 0;
    }
    return (uint64_t) res.tv_sec * 1000
           + (uint64_t) res.tv_nsec / (1000 * 1000);
}

#endif // ANJ_TIME_POSIX_COMPAT
