/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <inttypes.h>
#include <stdint.h>

#include "../anjay_core.h"
#include "../anjay_utils_private.h"

#include "anjay_dm_attributes.h"
#include "anjay_query.h"

VISIBILITY_SOURCE_BEGIN

#ifdef ANJAY_WITH_CON_ATTR
#    define _ANJAY_DM_CUSTOM_CON_ATTR_INITIALIZER \
        ,                                         \
                .con = ANJAY_DM_CON_ATTR_NONE
#else // ANJAY_WITH_CON_ATTR
#    define _ANJAY_DM_CUSTOM_CON_ATTR_INITIALIZER
#endif // ANJAY_WITH_CON_ATTR

#define _ANJAY_DM_OI_ATTRIBUTES_EMPTY                 \
    {                                                 \
        .min_period = ANJAY_ATTRIB_INTEGER_NONE,      \
        .max_period = ANJAY_ATTRIB_INTEGER_NONE,      \
        .min_eval_period = ANJAY_ATTRIB_INTEGER_NONE, \
        .max_eval_period = ANJAY_ATTRIB_INTEGER_NONE  \
                _ANJAY_DM_CUSTOM_CON_ATTR_INITIALIZER \
    }

#define _ANJAY_DM_R_ATTRIBUTES_EMPTY              \
    {                                             \
        .common = _ANJAY_DM_OI_ATTRIBUTES_EMPTY,  \
        .greater_than = ANJAY_ATTRIB_DOUBLE_NONE, \
        .less_than = ANJAY_ATTRIB_DOUBLE_NONE,    \
        .step = ANJAY_ATTRIB_DOUBLE_NONE          \
    }

const anjay_dm_oi_attributes_t ANJAY_DM_OI_ATTRIBUTES_EMPTY =
        _ANJAY_DM_OI_ATTRIBUTES_EMPTY;
const anjay_dm_r_attributes_t ANJAY_DM_R_ATTRIBUTES_EMPTY =
        _ANJAY_DM_R_ATTRIBUTES_EMPTY;

static inline void combine_integer(int32_t *out, int32_t other) {
    if (*out < 0) {
        *out = other;
    }
}

static inline void combine_double(double *out, double other) {
    if (isnan(*out)) {
        *out = other;
    }
}

static inline void combine_attrs(anjay_dm_oi_attributes_t *out,
                                 const anjay_dm_oi_attributes_t *other) {
#ifdef ANJAY_WITH_CON_ATTR
    if (out->con < 0) {
        out->con = other->con;
    }
#endif
    combine_integer(&out->min_period, other->min_period);
    combine_integer(&out->max_period, other->max_period);
    combine_integer(&out->min_eval_period, other->min_eval_period);
    combine_integer(&out->max_eval_period, other->max_eval_period);
}

static inline void
combine_resource_attrs(anjay_dm_r_attributes_t *out,
                       const anjay_dm_r_attributes_t *other) {
    combine_attrs(&out->common, &other->common);
    combine_double(&out->greater_than, other->greater_than);
    combine_double(&out->less_than, other->less_than);
    combine_double(&out->step, other->step);
}

int _anjay_read_period(anjay_unlocked_t *anjay,
                       anjay_iid_t server_iid,
                       anjay_rid_t rid,
                       int32_t *out) {
    int64_t value;
    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid, rid);

    int result = _anjay_dm_read_resource_i64(anjay, &path, &value);
    if (result == ANJAY_ERR_METHOD_NOT_ALLOWED
            || result == ANJAY_ERR_NOT_FOUND) {
        *out = ANJAY_ATTRIB_INTEGER_NONE;
        return 0;
    } else if (result < 0) {
        return result;
    } else if (value < 0 || value > INT32_MAX) {
        return ANJAY_ATTRIB_INTEGER_NONE;
    } else {
        *out = (int32_t) value;
        return 0;
    }
}

static int read_combined_period(anjay_unlocked_t *anjay,
                                anjay_iid_t server_iid,
                                anjay_rid_t rid,
                                int32_t *out) {
    if (*out < 0) {
        return _anjay_read_period(anjay, server_iid, rid, out);
    } else {
        return 0;
    }
}

int _anjay_dm_read_combined_server_attrs(anjay_unlocked_t *anjay,
                                         anjay_ssid_t ssid,
                                         anjay_dm_oi_attributes_t *out) {
    if (out->min_period >= 0 && out->max_period >= 0) {
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
                                           &out->min_period))
                || (result = read_combined_period(
                            anjay, server_iid, ANJAY_DM_RID_SERVER_DEFAULT_PMAX,
                            &out->max_period))) {
            return result;
        }
    }
    if (out->min_period < 0) {
        out->min_period = ANJAY_DM_DEFAULT_PMIN_VALUE;
    }
    return 0;
}

static int
dm_read_combined_resource_attrs(anjay_unlocked_t *anjay,
                                const anjay_dm_installed_object_t *obj,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_ssid_t ssid,
                                anjay_dm_r_attributes_t *out) {
    if (!_anjay_dm_resource_attributes_full(out)) {
        anjay_dm_r_attributes_t resattrs = ANJAY_DM_R_ATTRIBUTES_EMPTY;
        int result = _anjay_dm_call_resource_read_attrs(anjay, obj, iid, rid,
                                                        ssid, &resattrs);
        if (result) {
            return result;
        }
        combine_resource_attrs(out, &resattrs);
    }
    return 0;
}

static int
dm_read_combined_instance_attrs(anjay_unlocked_t *anjay,
                                const anjay_dm_installed_object_t *obj,
                                anjay_iid_t iid,
                                anjay_ssid_t ssid,
                                anjay_dm_oi_attributes_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_oi_attributes_t instattrs = ANJAY_DM_OI_ATTRIBUTES_EMPTY;
        int result =
                _anjay_dm_call_instance_read_default_attrs(anjay, obj, iid,
                                                           ssid, &instattrs);
        if (result) {
            return result;
        }
        combine_attrs(out, &instattrs);
    }
    return 0;
}

static int dm_read_combined_object_attrs(anjay_unlocked_t *anjay,
                                         const anjay_dm_installed_object_t *obj,
                                         anjay_ssid_t ssid,
                                         anjay_dm_oi_attributes_t *out) {
    if (!_anjay_dm_attributes_full(out)) {
        anjay_dm_oi_attributes_t objattrs = ANJAY_DM_OI_ATTRIBUTES_EMPTY;
        int result = _anjay_dm_call_object_read_default_attrs(anjay, obj, ssid,
                                                              &objattrs);
        if (result) {
            return result;
        }
        combine_attrs(out, &objattrs);
    }
    return 0;
}

bool _anjay_dm_attributes_empty(const anjay_dm_oi_attributes_t *attrs) {
    return attrs->min_period < 0 && attrs->max_period < 0
           && attrs->min_eval_period < 0 && attrs->max_eval_period < 0
#ifdef ANJAY_WITH_CON_ATTR
           && attrs->con < 0
#endif // ANJAY_WITH_CON_ATTR
            ;
}

bool _anjay_dm_resource_attributes_empty(const anjay_dm_r_attributes_t *attrs) {
    return _anjay_dm_attributes_empty(&attrs->common)
           && isnan(attrs->greater_than) && isnan(attrs->less_than)
           && isnan(attrs->step);
}

bool _anjay_dm_attributes_full(const anjay_dm_oi_attributes_t *attrs) {
    return attrs->min_period >= 0 && attrs->max_period >= 0
           && attrs->min_eval_period >= 0 && attrs->max_eval_period >= 0
#ifdef ANJAY_WITH_CON_ATTR
           && attrs->con >= 0
#endif // ANJAY_WITH_CON_ATTR
            ;
}

bool _anjay_dm_resource_attributes_full(const anjay_dm_r_attributes_t *attrs) {
    // _anjay_dm_attributes_full() already checks if
    // con != ANJAY_DM_CON_ATTR_NONE
    return _anjay_dm_attributes_full(&attrs->common)
           && !isnan(attrs->greater_than) && !isnan(attrs->less_than)
           && !isnan(attrs->step);
}

int _anjay_dm_effective_attrs(anjay_unlocked_t *anjay,
                              const anjay_dm_attrs_query_details_t *query,
                              anjay_dm_r_attributes_t *out) {
    int result = 0;
    *out = ANJAY_DM_R_ATTRIBUTES_EMPTY;

    if (query->obj) {
        assert(_anjay_uri_path_normalized(
                &MAKE_URI_PATH(_anjay_dm_installed_object_oid(query->obj),
                               query->iid, query->rid, query->riid)));

#ifdef ANJAY_WITH_LWM2M11
        if (query->riid != ANJAY_ID_INVALID) {
            result = _anjay_dm_call_resource_instance_read_attrs(
                    anjay, query->obj, query->iid, query->rid, query->riid,
                    query->ssid, out);
            if (result) {
                return result;
            }
        }
#endif // ANJAY_WITH_LWM2M11

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
                    anjay, query->obj, query->iid, query->ssid, &out->common);
            if (result) {
                return result;
            }
        }

        result = dm_read_combined_object_attrs(anjay, query->obj, query->ssid,
                                               &out->common);
    }
    if (!result && query->with_server_level_attrs) {
        return _anjay_dm_read_combined_server_attrs(anjay, query->ssid,
                                                    &out->common);
    }
    return result;
}
