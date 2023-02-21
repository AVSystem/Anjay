/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_TEST_MOCK_CLOCK_H
#define ANJAY_TEST_MOCK_CLOCK_H

#include <avsystem/commons/avs_time.h>

void _anjay_mock_clock_start(const avs_time_monotonic_t t);
void _anjay_mock_clock_reset(const avs_time_monotonic_t t);
void _anjay_mock_clock_advance(const avs_time_duration_t t);
void _anjay_mock_clock_finish(void);

#endif /* ANJAY_TEST_MOCK_CLOCK_H */
