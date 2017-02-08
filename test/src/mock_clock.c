/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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
#include <config.h>

#include <dlfcn.h>

#include <avsystem/commons/list.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_modules/time.h>

#include <anjay_test/mock_clock.h>

static struct timespec MOCK_CLOCK = { 0, -1 };

void _anjay_mock_clock_start(const struct timespec *t) {
    MOCK_CLOCK = (const struct timespec) { 0, -1 };
    AVS_UNIT_ASSERT_FALSE(_anjay_time_is_valid(&MOCK_CLOCK));
    AVS_UNIT_ASSERT_TRUE(_anjay_time_is_valid(t));
    MOCK_CLOCK = *t;
}

void _anjay_mock_clock_advance(const struct timespec *t) {
    AVS_UNIT_ASSERT_TRUE(_anjay_time_is_valid(&MOCK_CLOCK));
    AVS_UNIT_ASSERT_TRUE(_anjay_time_is_valid(t));
    _anjay_time_add(&MOCK_CLOCK, t);
}

void _anjay_mock_clock_finish(void) {
    AVS_UNIT_ASSERT_TRUE(_anjay_time_is_valid(&MOCK_CLOCK));
}

static int (*orig_clock_gettime)(clockid_t, struct timespec *);

AVS_UNIT_GLOBAL_INIT(verbose) {
    (void) verbose;
    orig_clock_gettime = (int (*)(clockid_t, struct timespec *)) (intptr_t)
            dlsym(RTLD_NEXT, "clock_gettime");
}

int clock_gettime(clockid_t clock, struct timespec *t) {
    if (_anjay_time_is_valid(&MOCK_CLOCK)) {
        // all clocks are equivalent for our purposes, so ignore clock
        *t = MOCK_CLOCK;
        _anjay_time_add(&MOCK_CLOCK, &(const struct timespec) { 0, 1 });
        return 0;
    } else {
        return orig_clock_gettime(clock, t);
    }
}
