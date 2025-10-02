/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_CORELNK_H
#define ANJAY_IO_CORELNK_H

#include <anjay/core.h>

#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Function that returns a null-terminated string containing a list of objects,
 * their instances and version. It can be used as a payload for Register and
 * Update operations or as a /25/x/3 resource value.
 *
 * @param anjay        Anjay object to operate on.
 * @param dm           Data model on which the query will be performed.
 * @param version      LwM2M version for which the query will be prepared.
 * @param buffer       The pointer that will be set to the buffer with the
 *                     prepared string. It is the caller's responsibility to
 *                     free the buffer using avs_free().
 *
 * @returns 0 on success, a negative value in case of error.
 */
int _anjay_corelnk_query_dm(anjay_unlocked_t *anjay,
                            anjay_dm_t *dm,
                            anjay_lwm2m_version_t version,
                            char **buffer);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_IO_CORELNK_H
