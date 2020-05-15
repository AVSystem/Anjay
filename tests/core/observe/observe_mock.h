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

#ifndef ANJAY_TEST_OBSERVE_MOCK_H
#define ANJAY_TEST_OBSERVE_MOCK_H

#include <avsystem/commons/avs_unit_mock_helpers.h>

AVS_UNIT_MOCK_CREATE(_anjay_dm_find_object_by_oid)
#define _anjay_dm_find_object_by_oid(...) \
    AVS_UNIT_MOCK_WRAPPER(_anjay_dm_find_object_by_oid)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(send_initial_response)
#define send_initial_response(...) \
    AVS_UNIT_MOCK_WRAPPER(send_initial_response)(__VA_ARGS__)

#endif /* ANJAY_TEST_OBSERVE_MOCK_H */
