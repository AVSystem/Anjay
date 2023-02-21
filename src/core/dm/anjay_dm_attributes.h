/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
    const anjay_dm_installed_object_t *obj;
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
int _anjay_dm_effective_attrs(anjay_unlocked_t *anjay,
                              const anjay_dm_attrs_query_details_t *query,
                              anjay_dm_r_attributes_t *out);

/**
 * Reads an integer resource value for some server instance.
 * Designed to read values of Default Minimum/Maximum Period resources.
 *
 * @param anjay      ANJAY object to operate on.
 * @param server_iid Server instance id to read value from.
 * @param rid        Resource id.
 * @param out        Result of the read.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_read_period(anjay_unlocked_t *anjay,
                       anjay_iid_t server_iid,
                       anjay_rid_t rid,
                       int32_t *out);

/**
 * If Minimum/Maximum Period attribute is not present, it sets it to the value
 * of the Default Minimum/Maximum Period resource of the given server instance.
 *
 * @param anjay      ANJAY object to operate on.
 * @param ssid       SSID of the server.
 * @param out        Attributes to which te result values should be written.
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_dm_read_combined_server_attrs(anjay_unlocked_t *anjay,
                                         anjay_ssid_t ssid,
                                         anjay_dm_oi_attributes_t *out);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_DM_ATTRIBUTES_H
