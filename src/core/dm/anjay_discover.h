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

#ifndef ANJAY_DM_DISCOVER_H
#define ANJAY_DM_DISCOVER_H

#include <anjay/dm.h>

#include <anjay_modules/anjay_dm_utils.h>

#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_stream_v_table.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_DISCOVER
/**
 * Performs LwM2M Discover operation.
 *
 * If path refers to an Object:
 *  - lists all attributes assigned to the Object (for specified Server)
 *  - lists all Object Instances,
 *  - lists all present Resources for each Object Instance.
 *
 * If path refers to an Instance:
 *  - lists all attributes assigned to the Object Instance
 *  - lists all present Resources and their attributes for the specified Server
 *    (these are not inherited from upper levels).
 *
 * If path refers to a Resource:
 *  - lists all attributes assigned to this Resource
 *
 * @param anjay  ANJAY object to operate on.
 * @param stream Stream where result of Discover shall be written.
 * @param obj    Object on which Discover shall be performed.
 * @param iid    ID of the Object Instance on which Discover shall be performed,
 *               or ANJAY_ID_INVALID if it is to be performed on the Object.
 * @param rid    ID of the Resource on which Discover shall be performed, or
 *               ANJAY_ID_INVALID if it is to be performed on the Object or
 *               Object Instance.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_discover(anjay_t *anjay,
                    avs_stream_t *stream,
                    const anjay_dm_object_def_t *const *obj,
                    anjay_iid_t iid,
                    anjay_rid_t rid);

#    ifdef ANJAY_WITH_BOOTSTRAP
/**
 * Performs LwM2M Bootstrap Discover operation.
 *
 * @param anjay  ANJAY object to operate on.
 * @param stream Stream where result of Bootstrap Discover shall be written.
 * @param oid    ID of the Object on which to perform the operation - may also
 *               be ANJAY_ID_INVALID, in which case it will be interpreted as a
 *               Discover on the root path.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_bootstrap_discover(anjay_t *anjay,
                              avs_stream_t *stream,
                              anjay_oid_t oid);
#    endif // ANJAY_WITH_BOOTSTRAP

#endif // ANJAY_WITH_DISCOVER

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_DM_DISCOVER_H */
