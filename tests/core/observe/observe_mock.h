/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
