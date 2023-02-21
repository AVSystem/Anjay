/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_TEST_MOCK_CLOCK_H
#define AVS_TEST_MOCK_CLOCK_H

#include <avsystem/commons/avs_time.h>

void _avs_mock_clock_start(const avs_time_monotonic_t t);
void _avs_mock_clock_advance(const avs_time_duration_t t);
void _avs_mock_clock_finish(void);

#endif /* AVS_TEST_MOCK_CLOCK_H */
