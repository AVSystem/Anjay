/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <time.h>

#include <avsystem/commons/avs_time.h>

#include "demo.h"

static avs_time_duration_t TIME_OFFSET;
static pthread_mutex_t TIME_OFFSET_MUTEX = PTHREAD_MUTEX_INITIALIZER;

avs_time_real_t avs_time_real_now(void) {
    struct timespec system_value;
    avs_time_real_t result;
    clock_gettime(CLOCK_REALTIME, &system_value);
    result.since_real_epoch.seconds = system_value.tv_sec;
    result.since_real_epoch.nanoseconds = (int32_t) system_value.tv_nsec;
    pthread_mutex_lock(&TIME_OFFSET_MUTEX);
    result = avs_time_real_add(result, TIME_OFFSET);
    pthread_mutex_unlock(&TIME_OFFSET_MUTEX);
    return result;
}

avs_time_monotonic_t avs_time_monotonic_now(void) {
    struct timespec system_value;
    avs_time_monotonic_t result;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &system_value))
#endif
    {
        // CLOCK_MONOTONIC is not mandatory in POSIX;
        // fallback to REALTIME if we don't have it
        clock_gettime(CLOCK_REALTIME, &system_value);
    }
    result.since_monotonic_epoch.seconds = system_value.tv_sec;
    result.since_monotonic_epoch.nanoseconds = (int32_t) system_value.tv_nsec;
    pthread_mutex_lock(&TIME_OFFSET_MUTEX);
    result = avs_time_monotonic_add(result, TIME_OFFSET);
    pthread_mutex_unlock(&TIME_OFFSET_MUTEX);
    return result;
}

void demo_advance_time(avs_time_duration_t duration) {
    pthread_mutex_lock(&TIME_OFFSET_MUTEX);
    TIME_OFFSET = avs_time_duration_add(TIME_OFFSET, duration);
    pthread_mutex_unlock(&TIME_OFFSET_MUTEX);
}
