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

#ifndef ANJAY_ACCESS_UTILS_PRIVATE_H
#define ANJAY_ACCESS_UTILS_PRIVATE_H

#include "anjay_core.h"
#include "anjay_dm_core.h"

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
 * NOTE: The instance ID may be @ref ANJAY_ID_INVALID only if the operation is
 * Create.
 */
bool _anjay_instance_action_allowed(anjay_t *anjay,
                                    const anjay_action_info_t *info);

/**
 * Performs implicit creations and deletions of Access Control object instances
 * according to data model changes.
 *
 * Specifically, it performs three steps:
 *
 * 1. Removes all Access Control object instances that refer to Object Instances
 *    that have been removed from the data model.
 * 2. If there were changes to the Security object, removes all ACL entries
 *    (i.e., ACL Resource Instances) that refer to SSIDs of Servers who are no
 *    longer repesented in the data model. This may cause changing the owner of
 *    those Access Control object instances which have multiple ACL entries, or
 *    removal of instances for which the ACL would be empty. In the latter case,
 *    the referred Object Instances are removed as well (see LwM2M TS 1.0.2,
 *    E.1.3 Unbootstrapping).
 * 3. Creates new Access Control object instances that refer to all newly
 *    created Object Instances. These will have the owner and the default ACL
 *    referring to SSID == _anjay_dm_current_ssid(anjay).
 *
 * Please refer to comments inside the implementation for details.
 */
int _anjay_sync_access_control(anjay_t *anjay,
                               anjay_notify_queue_t incoming_queue);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_ACCESS_UTILS_PRIVATE_H */
