/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <inttypes.h>
#include <stdint.h>

#include "../anjay_core.h"
#include "../utils_core.h"

#include "dm_attributes.h"
#include "query.h"

VISIBILITY_SOURCE_BEGIN

const anjay_dm_attributes_t ANJAY_DM_ATTRIBS_EMPTY = _ANJAY_DM_ATTRIBS_EMPTY;
const anjay_dm_resource_attributes_t ANJAY_RES_ATTRIBS_EMPTY =
        _ANJAY_RES_ATTRIBS_EMPTY;

const anjay_dm_internal_attrs_t ANJAY_DM_INTERNAL_ATTRS_EMPTY =
        _ANJAY_DM_INTERNAL_ATTRS_EMPTY;
const anjay_dm_internal_res_attrs_t ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY =
        _ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;

static inline void combine_period(int32_t *out, int32_t other) {
    if (*out < 0) {
        *out = other;
    }
}

static inline void combine_attrs(anjay_dm_internal_attrs_t *out,
                                 const anjay_dm_internal_attrs_t *other) {
#ifdef WITH_CON_ATTR
    if (out->custom.data.con < 0) {
        out->custom.data.con = other->custom.data.con;
    }
#endif
    combine_period(&out->standard.min_period, other->standard.min_period);
    combine_period(&out->standard.max_period, other->standard.max_period);
}

static int read_period(anjay_t *anjay,
                       anjay_iid_t server_iid,
                       anjay_rid_t rid,
                       int32_t *out) {
    int64_t value;
    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid, rid);

    int result = _anjay_dm_res_read_i64(anjay, &path, &value);
    if (result == ANJAY_ERR_METHOD_NOT_ALLOWED
            || result == ANJAY_ERR_NOT_FOUND) {
        *out = ANJAY_ATTRIB_PERIOD_NONE;
        return 0;
    } else if (result < 0) {
        return result;
    } else if (value < 0 || value > INT32_MAX) {
        return ANJAY_ATTRIB_PERIOD_NONE;
    } else {
        *out = (int32_t) value;
        return 0;
    }
}

static int read_combined_period(anjay_t *anjay,
                                anjay_iid_t server_iid,
                                anjay_rid_t rid,
                                int32_t *out) {
    if (*out < 0) {
        return read_period(anjay, server_iid, rid, out);
    } else {
        return 0;
    }
}

int _anjay_dm_read_combined_server_attrs(anjay_t *anjay,
                                         anjay_ssid_t ssid,
                                         anjay_dm_internal_attrs_t *out) {
    if (out->standard.min_period >= 0 && out->standard.max_period >= 0) {
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
                                           &out->standard.min_period))
                || (result = read_combined_period(
                            anjay, server_iid, ANJAY_DM_RID_SERVER_DEFAULT_PMAX,
                            &out->standard.max_period))) {
            return result;
        }
    }
    if (out->standard.min_period < 0) {
        out->standard.min_period = ANJAY_DM_DEFAULT_PMIN_VALUE;
    }
    return 0;
}

int _anjay_dm_read_combined_instance_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_internal_attrs_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_internal_attrs_t instattrs = ANJAY_DM_INTERNAL_ATTRS_EMPTY;
        int result =
                _anjay_dm_instance_read_default_attrs(anjay, obj, iid, ssid,
                                                      &instattrs, NULL);
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
        anjay_dm_internal_attrs_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_internal_attrs_t objattrs = ANJAY_DM_INTERNAL_ATTRS_EMPTY;
        int result = _anjay_dm_object_read_default_attrs(anjay, obj, ssid,
                                                         &objattrs, NULL);
        if (result) {
            return result;
        }
        combine_attrs(out, &objattrs);
    }
    return 0;
}

bool _anjay_dm_attributes_empty(const anjay_dm_internal_attrs_t *attrs) {
    return attrs->standard.min_period < 0 && attrs->standard.max_period < 0
#ifdef WITH_CON_ATTR
           && attrs->custom.data.con < 0
#endif
            ;
}

bool _anjay_dm_resource_attributes_empty(
        const anjay_dm_internal_res_attrs_t *attrs) {
    return _anjay_dm_attributes_empty(
                   _anjay_dm_get_internal_attrs_const(&attrs->standard.common))
           && isnan(attrs->standard.greater_than)
           && isnan(attrs->standard.less_than) && isnan(attrs->standard.step);
}

bool _anjay_dm_attributes_full(const anjay_dm_internal_attrs_t *attrs) {
    return attrs->standard.min_period >= 0 && attrs->standard.max_period >= 0
#ifdef WITH_CON_ATTR
           && attrs->custom.data.con >= 0
#endif
            ;
}

int _anjay_dm_effective_attrs(anjay_t *anjay,
                              const anjay_dm_attrs_query_details_t *query,
                              anjay_dm_internal_res_attrs_t *out) {
    int result = 0;
    assert(query->rid <= UINT16_MAX);
    assert(!(query->iid == ANJAY_IID_INVALID && query->rid >= 0));
    *out = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;

    if (query->obj && *query->obj) {
        if (query->rid >= 0) {
            result =
                    _anjay_dm_resource_read_attrs(anjay, query->obj, query->iid,
                                                  (anjay_rid_t) query->rid,
                                                  query->ssid, out, NULL);
            if (result) {
                return result;
            }
        }

        if (query->iid != ANJAY_IID_INVALID) {
            result = _anjay_dm_read_combined_instance_attrs(
                    anjay, query->obj, query->iid, query->ssid,
                    _anjay_dm_get_internal_attrs(&out->standard.common));
            if (result) {
                return result;
            }
        }

        result = _anjay_dm_read_combined_object_attrs(
                anjay, query->obj, query->ssid,
                _anjay_dm_get_internal_attrs(&out->standard.common));
    }
    if (!result && query->with_server_level_attrs) {
        return _anjay_dm_read_combined_server_attrs(
                anjay, query->ssid,
                _anjay_dm_get_internal_attrs(&out->standard.common));
    }
    return result;
}
