/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE // for RTLD_NEXT
#include <avs_coap_config.h>

#define MODULE_NAME test
#include <x_log_config.h>

#include <dlfcn.h>
#include <time.h>

#include <avsystem/commons/list.h>
#include <avsystem/commons/unit/test.h>

#include <test/mock_clock.h>

static avs_time_monotonic_t MOCK_CLOCK = { { 0, -1 } };

void _avs_mock_clock_start(const avs_time_monotonic_t t) {
    MOCK_CLOCK = AVS_TIME_MONOTONIC_INVALID;
    AVS_UNIT_ASSERT_TRUE(avs_time_monotonic_valid(t));
    MOCK_CLOCK = t;
}

void _avs_mock_clock_advance(const avs_time_duration_t t) {
    AVS_UNIT_ASSERT_TRUE(avs_time_monotonic_valid(MOCK_CLOCK));
    AVS_UNIT_ASSERT_TRUE(avs_time_duration_valid(t));
    MOCK_CLOCK = avs_time_monotonic_add(MOCK_CLOCK, t);
}

void _avs_mock_clock_finish(void) {
    AVS_UNIT_ASSERT_TRUE(avs_time_monotonic_valid(MOCK_CLOCK));
}

static int (*orig_clock_gettime)(clockid_t, struct timespec *);

AVS_UNIT_GLOBAL_INIT(verbose) {
    (void) verbose;
    typedef int (*clock_gettime_t)(clockid_t, struct timespec *);
    orig_clock_gettime =
            (clock_gettime_t) (intptr_t) dlsym(RTLD_NEXT, "clock_gettime");
}

int clock_gettime(clockid_t clock, struct timespec *t) {
    if (avs_time_monotonic_valid(MOCK_CLOCK)) {
        // all clocks are equivalent for our purposes, so ignore clock
        t->tv_sec = (time_t) MOCK_CLOCK.since_monotonic_epoch.seconds;
        t->tv_nsec = MOCK_CLOCK.since_monotonic_epoch.nanoseconds;
        MOCK_CLOCK = avs_time_monotonic_add(
                MOCK_CLOCK, avs_time_duration_from_scalar(1, AVS_TIME_NS));
        return 0;
    } else {
        return orig_clock_gettime(clock, t);
    }
}
