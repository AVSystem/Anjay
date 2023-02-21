/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef BOOTSTRAP_MOCK_H
#define BOOTSTRAP_MOCK_H

#include <avsystem/commons/avs_unit_mock_helpers.h>

AVS_UNIT_MOCK_CREATE(_anjay_notify_perform_without_servers)
#define _anjay_notify_perform_without_servers(...) \
    AVS_UNIT_MOCK_WRAPPER(_anjay_notify_perform_without_servers)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_dm_call_instance_remove)
#define _anjay_dm_call_instance_remove(...) \
    AVS_UNIT_MOCK_WRAPPER(_anjay_dm_call_instance_remove)(__VA_ARGS__)

#endif /* BOOTSTRAP_MOCK_H */
