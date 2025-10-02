/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
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
#ifdef ANJAY_WITH_LWM2M_GATEWAY
    bool end_device;
#endif // ANJAY_WITH_LWM2M_GATEWAY
} anjay_action_info_t;

typedef enum {
    ANJAY_INSTANCE_ACTION_DISALLOWED,
    ANJAY_INSTANCE_ACTION_ALLOWED,
#ifdef ANJAY_WITH_ACCESS_CONTROL
    ANJAY_INSTANCE_ACTION_NEEDS_ACL_CHECK
#endif // ANJAY_WITH_ACCESS_CONTROL
} anjay_instance_action_allowed_stateless_result_t;

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
bool _anjay_instance_action_allowed(anjay_unlocked_t *anjay,
                                    const anjay_action_info_t *info);

/**
 * Checks whether an operation described by the @p info on a non-restricted
 * Object is allowed, but only if that can be determined without accessing the
 * data model.
 *
 * Returns @ref ANJAY_INSTANCE_ACTION_NEEDS_ACL_CHECK if it is not possible.
 */
anjay_instance_action_allowed_stateless_result_t
_anjay_instance_action_allowed_stateless(anjay_unlocked_t *anjay,
                                         const anjay_action_info_t *info);

#ifdef ANJAY_WITH_ACCESS_CONTROL
/**
 * Accesses the data model to check whether an operation described by @p info
 * is allowed.
 *
 * May only be called on an @p info object previously checked using @ref
 * _anjay_instance_action_allowed_stateless which returned @ref
 * ANJAY_INSTANCE_ACTION_NEEDS_ACL_CHECK result. The behavior is undefined
 * otherwise.
 *
 * @ref _anjay_instance_action_allowed_stateless and @ref
 * _anjay_instance_action_allowed_by_acl together shall have identical semantics
 * to @ref _anjay_instance_action_allowed.
 */
bool _anjay_instance_action_allowed_by_acl(anjay_unlocked_t *anjay,
                                           const anjay_action_info_t *info);
#endif // ANJAY_WITH_ACCESS_CONTROL

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
 *    longer represented in the data model. This may cause changing the owner of
 *    those Access Control object instances which have multiple ACL entries, or
 *    removal of instances for which the ACL would be empty. In the latter case,
 *    the referred Object Instances are removed as well (see LwM2M TS 1.0.2,
 *    E.1.3 Unbootstrapping).
 * 3. Creates new Access Control object instances that refer to all newly
 *    created Object Instances. These will have the owner and the default ACL
 *    referring to SSID == <c>origin_ssid</c> parameter.
 *
 * Please refer to comments inside the implementation for details.
 */
int _anjay_sync_access_control(anjay_unlocked_t *anjay,
                               anjay_ssid_t origin_ssid,
                               anjay_notify_queue_t *notifications_queue);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_ACCESS_UTILS_PRIVATE_H */
