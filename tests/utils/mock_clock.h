/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_TEST_MOCK_CLOCK_H
#define ANJAY_TEST_MOCK_CLOCK_H

#include <avsystem/commons/avs_time.h>

void _anjay_mock_clock_start(const avs_time_monotonic_t t);
void _anjay_mock_clock_reset(const avs_time_monotonic_t t);
void _anjay_mock_clock_advance(const avs_time_duration_t t);
void _anjay_mock_clock_finish(void);

#endif /* ANJAY_TEST_MOCK_CLOCK_H */
