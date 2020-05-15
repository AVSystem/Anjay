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

#ifndef ANJAY_DM_ATTRIBUTES_H
#define ANJAY_DM_ATTRIBUTES_H
#include <anjay_modules/anjay_dm_utils.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ANJAY_ATTR_PMIN "pmin"
#define ANJAY_ATTR_PMAX "pmax"
#define ANJAY_ATTR_EPMIN "epmin"
#define ANJAY_ATTR_EPMAX "epmax"
#define ANJAY_ATTR_GT "gt"
#define ANJAY_ATTR_LT "lt"
#define ANJAY_ATTR_ST "st"
#define ANJAY_ATTR_SSID "ssid"

#define ANJAY_CUSTOM_ATTR_CON "con"

typedef struct {
    /** Object whose Instance is being queried. */
    const anjay_dm_object_def_t *const *obj;
    /** Instance whose Resource is being queried. */
    anjay_iid_t iid;
    /**
     * Resource whose Attributes are being queried, or ANJAY_ID_INVALID in case
     * when query on an Instance is only performed.
     */
    anjay_rid_t rid;
    /**
     * Resource Instance whose Attributes are being queried, or ANJAY_ID_INVALID
     * in case when query on a Resource is only performed.
     */
    anjay_riid_t riid;
    /** Server, for which Attributes shall be obtained. */
    anjay_ssid_t ssid;
    /**
     * true if no matter what we are interested in inherited Server level
     * attributes.
     */
    bool with_server_level_attrs;
} anjay_dm_attrs_query_details_t;

/**
 * Obtains attributes for a specific LwM2M path by combining attributes from
 * different levels.
 *
 * WARNING: This function does not check whether path is valid, i.e. whether
 * Resource and/or Instance is present - caller must ensure that it is indeed
 * the case.
 *
 * Attribute inheritance logic (assuming Resource and Instance ids are
 * provided):
 *
 *  0. Set *out to ANJAY_DM_INTERNAL_R_ATTRS_EMPTY.
 *  1. Read Resource Instance attributes and combine them with *out attributes.
 *  2. Read Resource attributes and combine them with *out attributes.
 *  3. Read Instance attributes and combine them with *out attributes.
 *  4. Read Object attributes and combine them with *out attributes.
 *  5. (If with_server_level_attrs is set) Read Server attributes and combine
 *     them with *out attributes.
 *
 * Additional information:
 * - If any step from above fails, then the function returns negative value.
 * - If @p query->rid is negative, then attributes of the Resource are not
 *   queried.
 * - If @p query->iid is ANJAY_ID_INVALID, then attributes of the Instance
 *   are not queried.
 *
 * @param anjay     ANJAY object to operate on.
 * @param query     Query details.
 * @param out       Result of query.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_dm_effective_attrs(anjay_t *anjay,
                              const anjay_dm_attrs_query_details_t *query,
                              anjay_dm_internal_r_attrs_t *out);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_DM_ATTRIBUTES_H
