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

#ifndef ACCESS_UTILS_H
#define ACCESS_UTILS_H

#include "anjay_core.h"
#include "dm_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_ssid_t ssid;
    anjay_request_action_t action;
} anjay_action_info_t;

/**
 * Checks whether an operation described by the @p info on a non-restricted
 * Object is allowed. Security checks for restricted objects shall be performed
 * elsewhere.
 *
 * Restricted Objects in LwM2M 1.0 are:
 *  - Security Object (/0)
 *
 * NOTE: The instance ID may be @ref ANJAY_IID_INVALID only if the operation is
 * Create.
 */
bool _anjay_instance_action_allowed(anjay_t *anjay,
                                    const anjay_action_info_t *info);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ACCESS_UTILS_H */
