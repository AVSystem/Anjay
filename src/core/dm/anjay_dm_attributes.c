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

#include <anjay_init.h>

#include <inttypes.h>
#include <stdint.h>

#include "../anjay_core.h"
#include "../anjay_utils_private.h"

#include "anjay_dm_attributes.h"
#include "anjay_query.h"

VISIBILITY_SOURCE_BEGIN

const anjay_dm_oi_attributes_t ANJAY_DM_OI_ATTRIBUTES_EMPTY =
        _ANJAY_DM_OI_ATTRIBUTES_EMPTY;
const anjay_dm_r_attributes_t ANJAY_DM_R_ATTRIBUTES_EMPTY =
        _ANJAY_DM_R_ATTRIBUTES_EMPTY;

const anjay_dm_internal_oi_attrs_t ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY =
        _ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
const anjay_dm_internal_r_attrs_t ANJAY_DM_INTERNAL_R_ATTRS_EMPTY =
        _ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;

static inline void combine_period(int32_t *out, int32_t other) {
    if (*out < 0) {
        *out = other;
    }
}

static inline void combine_value(double *out, double other) {
    if (isnan(*out)) {
        *out = other;
    }
}

static inline void combine_attrs(anjay_dm_internal_oi_attrs_t *out,
                                 const anjay_dm_internal_oi_attrs_t *other) {
#ifdef ANJAY_WITH_CON_ATTR
    if (out->custom.data.con < 0) {
        out->custom.data.con = other->custom.data.con;
    }
#endif
    combine_period(&out->standard.min_period, other->standard.min_period);
    combine_period(&out->standard.max_period, other->standard.max_period);
    combine_period(&out->standard.min_eval_period,
                   other->standard.min_eval_period);
    combine_period(&out->standard.max_eval_period,
                   other->standard.max_eval_period);
}

static inline void
combine_resource_attrs(anjay_dm_internal_r_attrs_t *out,
                       const anjay_dm_internal_r_attrs_t *other) {
    combine_attrs(_anjay_dm_get_internal_oi_attrs(&out->standard.common),
                  _anjay_dm_get_internal_oi_attrs_const(
                          &other->standard.common));
#ifdef ANJAY_WITH_CON_ATTR
    if (out->custom.data.con < 0) {
        out->custom.data.con = other->custom.data.con;
    }
#endif
    combine_value(&out->standard.greater_than, other->standard.greater_than);
    combine_value(&out->standard.less_than, other->standard.less_than);
    combine_value(&out->standard.step, other->standard.step);
}

static int read_period(anjay_t *anjay,
                       anjay_iid_t server_iid,
                       anjay_rid_t rid,
                       int32_t *out) {
    int64_t value;
    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid, rid);

    int result = _anjay_dm_read_resource_i64(anjay, &path, &value);
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

static int dm_read_combined_server_attrs(anjay_t *anjay,
                                         anjay_ssid_t ssid,
                                         anjay_dm_internal_oi_attrs_t *out) {
    if (out->standard.min_period >= 0 && out->standard.max_period >= 0) {
        return 0;
    }
    anjay_iid_t server_iid = ANJAY_ID_INVALID;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        anjay_log(
                WARNING,
                _("Could not find Server IID for Short Server ID: ") "%" PRIu16,
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

static int
dm_read_combined_resource_attrs(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_ssid_t ssid,
                                anjay_dm_internal_r_attrs_t *out) {
    if (!_anjay_dm_resource_attributes_full(out)) {
        anjay_dm_internal_r_attrs_t resattrs = ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;
        int result = _anjay_dm_call_resource_read_attrs(anjay, obj, iid, rid,
                                                        ssid, &resattrs, NULL);
        if (result) {
            return result;
        }
        combine_resource_attrs(out, &resattrs);
    }
    return 0;
}

static int
dm_read_combined_instance_attrs(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj,
                                anjay_iid_t iid,
                                anjay_ssid_t ssid,
                                anjay_dm_internal_oi_attrs_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_internal_oi_attrs_t instattrs =
                ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
        int result = _anjay_dm_call_instance_read_default_attrs(
                anjay, obj, iid, ssid, &instattrs, NULL);
        if (result) {
            return result;
        }
        combine_attrs(out, &instattrs);
    }
    return 0;
}

static int
dm_read_combined_object_attrs(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              anjay_ssid_t ssid,
                              anjay_dm_internal_oi_attrs_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_internal_oi_attrs_t objattrs =
                ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
        int result = _anjay_dm_call_object_read_default_attrs(anjay, obj, ssid,
                                                              &objattrs, NULL);
        if (result) {
            return result;
        }
        combine_attrs(out, &objattrs);
    }
    return 0;
}

bool _anjay_dm_attributes_empty(const anjay_dm_internal_oi_attrs_t *attrs) {
    return attrs->standard.min_period < 0 && attrs->standard.max_period < 0
           && attrs->standard.min_eval_period < 0
           && attrs->standard.max_eval_period < 0
#ifdef ANJAY_WITH_CON_ATTR
           && attrs->custom.data.con < 0
#endif // ANJAY_WITH_CON_ATTR
            ;
}

bool _anjay_dm_resource_attributes_empty(
        const anjay_dm_internal_r_attrs_t *attrs) {
    return _anjay_dm_attributes_empty(_anjay_dm_get_internal_oi_attrs_const(
                   &attrs->standard.common))
           && isnan(attrs->standard.greater_than)
           && isnan(attrs->standard.less_than) && isnan(attrs->standard.step);
}

bool _anjay_dm_attributes_full(const anjay_dm_internal_oi_attrs_t *attrs) {
    return attrs->standard.min_period >= 0 && attrs->standard.max_period >= 0
           && attrs->standard.min_eval_period >= 0
           && attrs->standard.max_eval_period >= 0
#ifdef ANJAY_WITH_CON_ATTR
           && attrs->custom.data.con >= 0
#endif // ANJAY_WITH_CON_ATTR
            ;
}

bool _anjay_dm_resource_attributes_full(
        const anjay_dm_internal_r_attrs_t *attrs) {
#ifdef ANJAY_WITH_CON_ATTR
    // _anjay_dm_attributes_full() already checks if
    // con != ANJAY_DM_CON_ATTR_DEFAULT
    AVS_ASSERT(
            &attrs->custom
                    == &_anjay_dm_get_internal_oi_attrs_const(
                                &attrs->standard.common)
                                ->custom,
            "There should be exactly ONE instance of "
            "anjay_dm_custom_attrs_storage_t in anjay_dm_internal_r_attrs_t");
#endif
    return _anjay_dm_attributes_full(_anjay_dm_get_internal_oi_attrs_const(
                   &attrs->standard.common))
           && !isnan(attrs->standard.greater_than)
           && !isnan(attrs->standard.less_than) && !isnan(attrs->standard.step);
}

int _anjay_dm_effective_attrs(anjay_t *anjay,
                              const anjay_dm_attrs_query_details_t *query,
                              anjay_dm_internal_r_attrs_t *out) {
    int result = 0;
    *out = ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;

    if (query->obj && *query->obj) {
        assert(_anjay_uri_path_normalized(&MAKE_URI_PATH(
                (*query->obj)->oid, query->iid, query->rid, query->riid)));

        if (query->rid != ANJAY_ID_INVALID) {
            result = dm_read_combined_resource_attrs(anjay, query->obj,
                                                     query->iid, query->rid,
                                                     query->ssid, out);
            if (result) {
                return result;
            }
        }

        if (query->iid != ANJAY_ID_INVALID) {
            result = dm_read_combined_instance_attrs(
                    anjay, query->obj, query->iid, query->ssid,
                    _anjay_dm_get_internal_oi_attrs(&out->standard.common));
            if (result) {
                return result;
            }
        }

        result = dm_read_combined_object_attrs(anjay, query->obj, query->ssid,
                                               _anjay_dm_get_internal_oi_attrs(
                                                       &out->standard.common));
    }
    if (!result && query->with_server_level_attrs) {
        return dm_read_combined_server_attrs(anjay, query->ssid,
                                             _anjay_dm_get_internal_oi_attrs(
                                                     &out->standard.common));
    }
    return result;
}
