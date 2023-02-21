/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_UTILS_H
#define ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_UTILS_H

#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    AVS_LIST(struct anjay_acl_ref_validation_object_info_struct) object_infos;
} anjay_acl_ref_validation_ctx_t;

/**
 * Creates a new validation context for use with
 * @ref _anjay_acl_ref_validate_inst_ref. This is necessary because it caches
 * the IID list to limit the number of calls to list_instances handler, and for
 * the purpose of duplicate checking.
 */
static inline anjay_acl_ref_validation_ctx_t
_anjay_acl_ref_validation_ctx_new(void) {
    return (anjay_acl_ref_validation_ctx_t) { NULL };
}

void _anjay_acl_ref_validation_ctx_cleanup(anjay_acl_ref_validation_ctx_t *ctx);

/**
 * Validates whether the target instance reference inside an Access Control
 * object. The validation fails on one of the following conditions:
 *
 * - Object with OID == target_oid does not exist in the data model
 * - target_iid is not 65535, and instance with IID == target_iid does not exist
 *   in the object
 * - Validation of the same (target_oid, target_iid) pair is attempted more than
 *   once for the same ctx
 */
int _anjay_acl_ref_validate_inst_ref(anjay_unlocked_t *anjay,
                                     anjay_acl_ref_validation_ctx_t *ctx,
                                     anjay_oid_t target_oid,
                                     anjay_iid_t target_iid);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_UTILS_H */
