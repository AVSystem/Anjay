/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_DM_DISCOVER_H
#define ANJAY_DM_DISCOVER_H

#include <anjay/dm.h>

#include <anjay_modules/anjay_dm_utils.h>

#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_stream_v_table.h>

#include "../anjay_utils_private.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_DISCOVER
/**
 * Performs LwM2M Discover operation.
 *
 * @param anjay         ANJAY object to operate on.
 * @param stream        Stream where result of Discover shall be written.
 * @param obj           Object on which Discover shall be performed.
 * @param iid           ID of the Object Instance on which Discover shall be
 *                      performed,
 *                      or ANJAY_ID_INVALID if it is to be performed on the
 *                      Object.
 * @param rid           ID of the Resource on which Discover shall be performed,
 *                      or ANJAY_ID_INVALID if it is to be performed on the
 *                      Object or Object Instance.
 * @param depth         Number of nesting levels into which to recursively
 *                      perform the discover operation.
 * @param ssid          SSID of the server for which the operation is performed.
 * @param lwm2m_version LwM2M version to which the response shall comply.
 *
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_discover(anjay_unlocked_t *anjay,
                    avs_stream_t *stream,
                    const anjay_dm_installed_object_t *obj,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    uint8_t depth,
                    anjay_ssid_t ssid,
                    anjay_lwm2m_version_t lwm2m_version);

#    ifdef ANJAY_WITH_BOOTSTRAP
/**
 * Performs LwM2M Bootstrap Discover operation.
 *
 * @param anjay         ANJAY object to operate on.
 * @param stream        Stream where result of Bootstrap Discover shall be
 *                      written.
 * @param oid           ID of the Object on which to perform the operation - may
 *                      also be ANJAY_ID_INVALID, in which case it will be
 *                      interpreted as a Discover on the root path.
 * @param lwm2m_version LwM2M version to which the response shall comply.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_bootstrap_discover(anjay_unlocked_t *anjay,
                              avs_stream_t *stream,
                              anjay_oid_t oid,
                              anjay_lwm2m_version_t lwm2m_version);
#    endif // ANJAY_WITH_BOOTSTRAP

#endif // ANJAY_WITH_DISCOVER

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_DM_DISCOVER_H */
