/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <anjay/anjay.h>
#include <avsystem/commons/stream_v_table.h>
#include <avsystem/commons/stream.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef WITH_DISCOVER
/**
 * Performs LWM2M Discover operation on specified Object:
 *  - lists all attributes assigned to the Object (for specified Server)
 *  - lists all Object Instances,
 *  - lists all present Resources for each Object Instance.
 *
 * @param anjay     ANJAY object to operate on.
 * @param obj       Object on which Discover shall be performed.
 * @param ssid      Short Server ID issuing Discover.
 * @param stream    Stream where results will be written.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_discover_object(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj,
                           anjay_ssid_t ssid,
                           avs_stream_abstract_t *stream);

/**
 * Performs LWM2M Discover operation on Object Instance:
 *  - lists all attributes assigned to the Object Instance
 *  - lists all present Resources and their attributes for the specified Server
 *    (these are not inherited from upper levels).
 *
 * @param anjay     ANJAY object to operate on.
 * @param obj       Object whose instance is being queried.
 * @param iid       Instance on which Discover shall be performed.
 * @param ssid      Short Server ID issuing Discover.
 * @param stream    Stream where results will be written.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_discover_instance(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_ssid_t ssid,
                             avs_stream_abstract_t *stream);

/**
 * Performs LWM2M Discover operation on Resource:
 *  - lists all attributes assigned to this Resource
 *
 * @param anjay     ANJAY object to operate on.
 * @param obj       Object whose resource is being queried.
 * @param iid       Instance whose resource is being queried.
 * @param rid       Resource on which Discover shall be performed.
 * @param ssid      Short Server ID issuing Discover.
 * @param stream    Stream where results will be written.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_discover_resource(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_ssid_t ssid,
                             avs_stream_abstract_t *stream);

#ifdef WITH_BOOTSTRAP
/**
 * Performs LWM2M Bootstrap Discover operation on the specified Object @p obj.
 *
 * @param anjay     ANJAY object to operate on.
 * @param obj       Object on which Discover is issued.
 * @param stream    Stream where results will be written.
 * @retrurn 0 on success, negative value in case of an error.
 */
int _anjay_bootstrap_discover_object(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     avs_stream_abstract_t *stream);
/**
 * Performs LWM2M Bootstrap Discover operation on the entire data model.
 *
 * @param anjay     ANJAY object to operate on.
 * @param stream    Stream where results will be written.
 * @retrurn 0 on success, negative value in case of an error.
 */
int _anjay_bootstrap_discover(anjay_t *anjay, avs_stream_abstract_t *stream);
#endif // WITH_BOOTSTRAP

#endif // WITH_DISCOVER

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_DM_DISCOVER_H */
