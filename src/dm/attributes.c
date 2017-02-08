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

#include <config.h>
#include <stdint.h>
#include <inttypes.h>

#include "../anjay.h"
#include "../utils.h"

#include "attributes.h"
#include "query.h"

VISIBILITY_SOURCE_BEGIN

const anjay_dm_attributes_t ANJAY_DM_ATTRIBS_EMPTY = _ANJAY_DM_ATTRIBS_EMPTY;

static inline void combine_period(time_t *out, time_t other) {
    if (*out < 0) {
        *out = other;
    }
}

static inline void combine_value(double *out, double other) {
    if (isnan(*out)) {
        *out = other;
    }
}

static inline void combine_attrs(anjay_dm_attributes_t *out,
                                 const anjay_dm_attributes_t *other) {
    combine_period(&out->min_period, other->min_period);
    combine_period(&out->max_period, other->max_period);
    combine_value(&out->greater_than, other->greater_than);
    combine_value(&out->less_than, other->less_than);
    combine_value(&out->step, other->step);
}

#define TIME_MAX (sizeof(time_t) == 8 ? INT64_MAX : INT32_MAX)

static int read_period(anjay_t *anjay,
                       anjay_iid_t server_iid,
                       anjay_rid_t rid,
                       time_t *out) {
    /* This enforces time_t to be a signed 32/64bit integer type. */
    AVS_STATIC_ASSERT((time_t) -1 < 0
                              && (sizeof(time_t) == 4 || sizeof(time_t) == 8)
                              && (time_t) 1.5 == (time_t) 1,
                      time_t_is_sane);

    int64_t value;
    const anjay_resource_path_t path = {
        ANJAY_DM_OID_SERVER, server_iid, rid
    };

    int result = _anjay_dm_res_read_i64(anjay, &path, &value);
    if (result == ANJAY_ERR_METHOD_NOT_ALLOWED
            || result == ANJAY_ERR_NOT_FOUND) {
        *out = ANJAY_ATTRIB_PERIOD_NONE;
        return 0;
    } else if (result < 0) {
        return result;
    } else if (value < 0 || value > TIME_MAX) {
        return ANJAY_ATTRIB_PERIOD_NONE;
    } else {
        *out = (time_t) value;
        return 0;
    }
}

static int read_combined_period(anjay_t *anjay,
                                anjay_iid_t server_iid,
                                anjay_rid_t rid,
                                time_t *out) {
    if (*out < 0) {
        return read_period(anjay, server_iid, rid, out);
    } else {
        return 0;
    }
}

int _anjay_dm_read_combined_server_attrs(anjay_t *anjay,
                                         anjay_ssid_t ssid,
                                         anjay_dm_attributes_t *out) {
    if (out->min_period >= 0 && out->max_period >= 0) {
        return 0;
    }
    anjay_iid_t server_iid = ANJAY_IID_INVALID;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        anjay_log(WARNING,
                  "Could not find Server IID for Short Server ID: %" PRIu16,
                  ssid);
    } else {
        int result;
        if ((result = read_combined_period(anjay, server_iid,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMIN,
                                           &out->min_period))
            || (result = read_combined_period(anjay, server_iid,
                                              ANJAY_DM_RID_SERVER_DEFAULT_PMAX,
                                              &out->max_period))) {
            return result;
        }
    }
    if (out->min_period < 0) {
        out->min_period = ANJAY_DM_DEFAULT_PMIN_VALUE;
    }
    return 0;
}

int _anjay_dm_read_combined_resource_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        anjay_dm_attributes_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_attributes_t resattrs = ANJAY_DM_ATTRIBS_EMPTY;
        int result = _anjay_dm_resource_read_attrs(anjay, obj, iid, rid, ssid,
                                                   &resattrs);
        if (result) {
            return result;
        }
        combine_attrs(out, &resattrs);
    }
    return 0;
}

int _anjay_dm_read_combined_instance_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_attributes_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_attributes_t instattrs = ANJAY_DM_ATTRIBS_EMPTY;
        int result = _anjay_dm_instance_read_default_attrs(anjay, obj, iid,
                                                           ssid, &instattrs);
        if (result) {
            return result;
        }
        combine_attrs(out, &instattrs);
    }
    return 0;
}

int _anjay_dm_read_combined_object_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_ssid_t ssid,
        anjay_dm_attributes_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_attributes_t objattrs = ANJAY_DM_ATTRIBS_EMPTY;
        int result = _anjay_dm_object_read_default_attrs(anjay, obj, ssid,
                                                         &objattrs);
        if (result) {
            return result;
        }
        combine_attrs(out, &objattrs);
    }
    return 0;
}

bool _anjay_dm_attributes_empty(const anjay_dm_attributes_t *attrs) {
    return attrs->min_period < 0
            && attrs->max_period < 0
            && isnan(attrs->greater_than)
            && isnan(attrs->less_than)
            && isnan(attrs->step);
}

bool _anjay_dm_attributes_full(const anjay_dm_attributes_t *attrs) {
    return attrs->min_period >= 0
            && attrs->max_period >= 0
            && !isnan(attrs->greater_than)
            && !isnan(attrs->less_than)
            && !isnan(attrs->step);
}

int _anjay_dm_effective_attrs(anjay_t *anjay,
                              const anjay_dm_attrs_query_details_t *query,
                              anjay_dm_attributes_t *out) {
    int result = 0;
    assert(query->rid <= UINT16_MAX);
    assert(!(query->iid == ANJAY_IID_INVALID && query->rid >= 0));
    *out = ANJAY_DM_ATTRIBS_EMPTY;

    if (query->rid >= 0) {
        result = _anjay_dm_read_combined_resource_attrs(
                anjay, query->obj, query->iid, (anjay_rid_t) query->rid,
                query->ssid, out);
        if (result) {
            return result;
        }
    }

    if (query->iid != ANJAY_IID_INVALID) {
        result = _anjay_dm_read_combined_instance_attrs(
                anjay, query->obj, query->iid, query->ssid, out);
        if (result) {
            return result;
        }
    }

    result = _anjay_dm_read_combined_object_attrs(anjay, query->obj, query->ssid, out);
    if (!result && query->with_server_level_attrs) {
        return _anjay_dm_read_combined_server_attrs(anjay, query->ssid, out);
    }
    return result;
}
