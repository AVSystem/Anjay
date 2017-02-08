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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_TIME_H
#define ANJAY_INCLUDE_ANJAY_MODULES_TIME_H

#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define NS_IN_S (1L * 1000L * 1000L * 1000L)

#define DAY_IN_S 86400

static const struct timespec ANJAY_TIME_ZERO = { 0, 0 };

static inline int _anjay_time_before(const struct timespec *a, const struct timespec *b) {
    return (a->tv_sec < b->tv_sec)
            || (a->tv_sec == b->tv_sec && a->tv_nsec < b->tv_nsec);
}

static inline int _anjay_time_is_valid(const struct timespec *t) {
    return (t->tv_nsec >= 0 && t->tv_nsec < NS_IN_S);
}

static inline void _anjay_time_add(struct timespec *result, const struct timespec *duration) {
    if (!_anjay_time_is_valid(result) || !_anjay_time_is_valid(duration)) {
        result->tv_sec = 0;
        result->tv_nsec = -1;
    } else {
        result->tv_sec += duration->tv_sec;
        result->tv_nsec += duration->tv_nsec;

        if (result->tv_nsec >= NS_IN_S) {
            result->tv_nsec -= NS_IN_S;
            ++result->tv_sec;
        }
    }

    assert(_anjay_time_is_valid(result));
}

static inline void _anjay_time_diff(struct timespec *result,
                                    const struct timespec *minuend,
                                    const struct timespec *subtrahend) {
    result->tv_sec = minuend->tv_sec - subtrahend->tv_sec;
    result->tv_nsec = minuend->tv_nsec - subtrahend->tv_nsec;
    if (result->tv_nsec < 0) {
        result->tv_nsec += NS_IN_S;
        --result->tv_sec;
    }

    assert(_anjay_time_is_valid(result));
}

static inline ssize_t _anjay_time_diff_ms(const struct timespec *minuend,
                                          const struct timespec *subtrahend) {
    struct timespec diff;
    _anjay_time_diff(&diff, minuend, subtrahend);
    return (ssize_t)(diff.tv_sec * 1000L + diff.tv_nsec / (1000L * 1000L));
}

static inline void _anjay_time_from_ms(struct timespec *result, int32_t ms) {
    result->tv_sec = (time_t) (ms / 1000);
    result->tv_nsec = (ms % 1000) * 1000000L;
    if (result->tv_nsec < 0) {
        result->tv_nsec += NS_IN_S;
        --result->tv_sec;
    }

    assert(_anjay_time_is_valid(result));
}

static inline void _anjay_time_from_s(struct timespec *result, time_t s) {
    result->tv_sec = s;
    result->tv_nsec = 0;
}

static inline void _anjay_time_add_ms(struct timespec *result,
                                      int32_t ms) {
    struct timespec duration;
    _anjay_time_from_ms(&duration, ms);
    _anjay_time_add(result, &duration);

    assert(_anjay_time_is_valid(result));
}

static inline void _anjay_time_div(struct timespec *result,
                                   const struct timespec *dividend,
                                   uint32_t divisor) {
    time_t s_rest = (time_t) (dividend->tv_sec % (int64_t) divisor);
    result->tv_sec = (time_t) (dividend->tv_sec / (int64_t) divisor);
    result->tv_nsec = (long)(((double)dividend->tv_nsec
                                + (double)s_rest * NS_IN_S)
                             / divisor);

    if (result->tv_nsec < 0) {
        result->tv_nsec += NS_IN_S;
        --result->tv_sec;
    }

    assert(_anjay_time_is_valid(result));
}

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_TIME_H */

