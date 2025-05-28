/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_LWM2M_GATEWAY_H
#define ANJAY_INCLUDE_ANJAY_MODULES_LWM2M_GATEWAY_H

#include "anjay_init.h"

#include <anjay_modules/anjay_attr_storage_utils.h>
#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_LWM2M_GATEWAY

/**
 * Maps URI prefix to target End Device Data Model
 *
 * @param      anjay   Anjay object to operate on
 * @param      prefix  DM prefix from the LwM2M request
 * @param[out] dm      End Device DM. If prefix is found in the Gateway scope,
 *                     this pointer is set to a proper DM, set to NULL otherwise
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int _anjay_lwm2m_gateway_prefix_to_dm(anjay_unlocked_t *anjay,
                                      const char *prefix,
                                      const anjay_dm_t **dm);

#    ifdef ANJAY_WITH_ATTR_STORAGE
/**
 * Maps URI prefix to target End Device Attribute Storage
 *
 * @param      anjay   Anjay object to operate on
 * @param      prefix  DM prefix from the LwM2M request
 * @param[out] as      End Device Attribute Storage. If prefix is found in the
 *                     Gateway scope, this pointer is set to a proper AS,
 *                     set to NULL otherwise
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int _anjay_lwm2m_gateway_prefix_to_as(anjay_unlocked_t *anjay,
                                      const char *prefix,
                                      anjay_attr_storage_t **as);
#    endif // ANJAY_WITH_ATTR_STORAGE

/**
 * Maps Instance ID to target End Device Data Model
 *
 * @param      anjay  Anjay object to operate on
 * @param      iid    End Device Instance ID
 * @param[out] dm     End Device DM. If prefix is found in the Gateway scope,
 *                    this pointer is set to a proper DM, set to NULL otherwise
 */
void _anjay_lwm2m_gateway_iid_to_dm(anjay_unlocked_t *anjay,
                                    anjay_iid_t iid,
                                    const anjay_dm_t **dm);

#endif // ANJAY_WITH_LWM2M_GATEWAY

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_INCLUDE_ANJAY_MODULES_LWM2M_GATEWAY_H
