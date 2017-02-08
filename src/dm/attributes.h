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

#ifndef ANJAY_DM_ATTRIBUTES_H
#define ANJAY_DM_ATTRIBUTES_H
#include <anjay_modules/dm.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define _ANJAY_DM_ATTRIBS_EMPTY { \
        .min_period = ANJAY_ATTRIB_PERIOD_NONE, \
        .max_period = ANJAY_ATTRIB_PERIOD_NONE, \
        .greater_than = ANJAY_ATTRIB_VALUE_NONE, \
        .less_than = ANJAY_ATTRIB_VALUE_NONE, \
        .step = ANJAY_ATTRIB_VALUE_NONE \
    }

#define ANJAY_ATTR_PMIN "pmin"
#define ANJAY_ATTR_PMAX "pmax"
#define ANJAY_ATTR_GT "gt"
#define ANJAY_ATTR_LT "lt"
#define ANJAY_ATTR_ST "st"
#define ANJAY_ATTR_SSID "ssid"

typedef struct {
    /** Object whose Instance is being queried. */
    const anjay_dm_object_def_t *const *obj;
    /** Instance whose Resource is being queried. */
    anjay_iid_t iid;
    /**
     * Resource whose Attributes are being queried, or negative value in case
     * when query on an Instance is only performed.
     */
    int32_t rid;
    /** Server, for which Attributes shall be obtained. */
    anjay_ssid_t ssid;
    /** true if no matter what we are interested in inherited Server level attributes. */
    bool with_server_level_attrs;
} anjay_dm_attrs_query_details_t;

/**
 * Obtains attributes for a specific LWM2M path by combining attributes from
 * different levels.
 *
 * WARNING: This function does not check whether path is valid, i.e. whether
 * Resource and/or Instance is present - caller must ensure that it is indeed
 * the case.
 *
 * Attribute inheritance logic (assuming Resource and Instance ids are provided):
 *  0. Set *out to ANJAY_DM_ATTRIBS_EMPTY.
 *  1. Read Resource attributes and combine them with *out attributes.
 *  2. Read Instance attributes and combine them with *out attributes.
 *  3. Read Object attributes and combine them with *out attributes.
 *  4. (If with_server_level_attrs is set) Read Server attributes and combine them
 *     with *out attributes.
 *
 * Additional information:
 * If any step from above fails, then the function returns negative value.
 * If @p query->rid is negative, then attributes of the Resource are not queried.
 * If @p query->iid is ANJAY_IID_INVALID, then attributes of the Instance are not queried.
 *
 * @param anjay     ANJAY object to operate on.
 * @param query     Query details.
 * @param out       Result of query.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_dm_effective_attrs(anjay_t *anjay,
                              const anjay_dm_attrs_query_details_t *query,
                              anjay_dm_attributes_t *out);

/**
 * Reads attributes assigned to the Resource (if *out has at least one unset
 * attribute) and combines them with *out.
 *
 * WARNING: This function does not perform any presence checks. Caller must
 * ensure this on its own.
 */
int _anjay_dm_read_combined_resource_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        anjay_dm_attributes_t *out);
/**
 * Reads attributes assigned to the Instance (if *out has at least one unset
 * attribute) and combines them with *out.
 *
 * WARNING: This function does not perform any presence checks. Caller must
 * ensure this on its own.
 */
int _anjay_dm_read_combined_instance_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_attributes_t *out);
/**
 * Reads attributes assigned to the Object (if *out has at least one unset
 * attribute) and combines them with *out.
 */
int _anjay_dm_read_combined_object_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_ssid_t ssid,
        anjay_dm_attributes_t *out);
/**
 * Reads Default Minimum Period and Default Maximum Period (if *out has not
 * set at least one of them) and combines them with *out.
 */
int _anjay_dm_read_combined_server_attrs(anjay_t *anjay,
                                         anjay_ssid_t ssid,
                                         anjay_dm_attributes_t *out);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_DM_ATTRIBUTES_H
