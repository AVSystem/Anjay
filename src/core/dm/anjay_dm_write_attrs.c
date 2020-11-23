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

#include "anjay_dm_write_attrs.h"

#include "../anjay_access_utils_private.h"

VISIBILITY_SOURCE_BEGIN

static void update_oi_attrs(anjay_dm_internal_oi_attrs_t *attrs_ptr,
                            const anjay_request_attributes_t *request_attrs) {
    if (request_attrs->has_min_period) {
        attrs_ptr->standard.min_period =
                request_attrs->values.standard.common.min_period;
    }
    if (request_attrs->has_max_period) {
        attrs_ptr->standard.max_period =
                request_attrs->values.standard.common.max_period;
    }
    if (request_attrs->has_min_eval_period) {
        attrs_ptr->standard.min_eval_period =
                request_attrs->values.standard.common.min_eval_period;
    }
    if (request_attrs->has_max_eval_period) {
        attrs_ptr->standard.max_eval_period =
                request_attrs->values.standard.common.max_eval_period;
    }
#ifdef ANJAY_WITH_CON_ATTR
    if (request_attrs->custom.has_con) {
        attrs_ptr->custom.data.con = request_attrs->values.custom.data.con;
    }
#endif
}

static void update_r_attrs(anjay_dm_internal_r_attrs_t *attrs_ptr,
                           const anjay_request_attributes_t *request_attrs) {
    update_oi_attrs(_anjay_dm_get_internal_oi_attrs(
                            &attrs_ptr->standard.common),
                    request_attrs);
    if (request_attrs->has_greater_than) {
        attrs_ptr->standard.greater_than =
                request_attrs->values.standard.greater_than;
    }
    if (request_attrs->has_less_than) {
        attrs_ptr->standard.less_than =
                request_attrs->values.standard.less_than;
    }
    if (request_attrs->has_step) {
        attrs_ptr->standard.step = request_attrs->values.standard.step;
    }
}

static bool oi_attrs_valid(const anjay_dm_internal_oi_attrs_t *attrs) {
    if (attrs->standard.min_eval_period >= 0
            && attrs->standard.max_eval_period >= 0
            && attrs->standard.min_eval_period
                           >= attrs->standard.max_eval_period) {
        dm_log(DEBUG, _("Attempted to set attributes that fail the 'epmin < "
                        "epmax' precondition"));
        return false;
    }
    return true;
}

static bool r_attrs_valid(const anjay_dm_internal_r_attrs_t *attrs) {
    if (!oi_attrs_valid(_anjay_dm_get_internal_oi_attrs_const(
                &attrs->standard.common))) {
        return false;
    }

    double step = 0.0;
    if (!isnan(attrs->standard.step)) {
        if (attrs->standard.step < 0.0) {
            dm_log(DEBUG, _("Attempted to set negative step attribute"));
            return false;
        }
        step = attrs->standard.step;
    }
    if (!isnan(attrs->standard.less_than)
            && !isnan(attrs->standard.greater_than)
            && attrs->standard.less_than + 2 * step
                           >= attrs->standard.greater_than) {
        dm_log(DEBUG, _("Attempted to set attributes that fail the 'lt + 2*st "
                        "< gt' precondition"));
        return false;
    }
    return true;
}

static inline bool
resource_specific_request_attrs_empty(const anjay_request_attributes_t *attrs) {
    return !attrs->has_greater_than && !attrs->has_less_than
           && !attrs->has_step;
}

static inline bool
request_attrs_empty(const anjay_request_attributes_t *attrs) {
    return !attrs->has_min_period && !attrs->has_max_period
           && !attrs->has_min_eval_period && !attrs->has_max_eval_period
#ifdef ANJAY_WITH_CON_ATTR
           && !attrs->custom.has_con
#endif
           && resource_specific_request_attrs_empty(attrs);
}

static int
dm_write_resource_attrs(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj,
                        anjay_iid_t iid,
                        anjay_rid_t rid,
                        const anjay_request_attributes_t *attributes) {
    anjay_dm_internal_r_attrs_t attrs = ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;
    int result;
    (void) ((result = _anjay_dm_verify_resource_present(anjay, obj, iid, rid,
                                                        NULL))
            || (result = _anjay_dm_call_resource_read_attrs(
                        anjay, obj, iid, rid, _anjay_dm_current_ssid(anjay),
                        &attrs, NULL)));
    if (!result) {
        update_r_attrs(&attrs, attributes);
        if (!r_attrs_valid(&attrs)) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            result = _anjay_dm_call_resource_write_attrs(
                    anjay, obj, iid, rid, _anjay_dm_current_ssid(anjay), &attrs,
                    NULL);
        }
    }
    return result;
}

static int
dm_write_instance_attrs(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj,
                        anjay_iid_t iid,
                        const anjay_request_attributes_t *attributes) {
    anjay_dm_internal_oi_attrs_t attrs = ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
    int result = _anjay_dm_call_instance_read_default_attrs(
            anjay, obj, iid, _anjay_dm_current_ssid(anjay), &attrs, NULL);
    if (!result) {
        update_oi_attrs(&attrs, attributes);
        if (!oi_attrs_valid(&attrs)) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            result = _anjay_dm_call_instance_write_default_attrs(
                    anjay, obj, iid, _anjay_dm_current_ssid(anjay), &attrs,
                    NULL);
        }
    }
    return result;
}

static int dm_write_object_attrs(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 const anjay_request_attributes_t *attributes) {
    anjay_dm_internal_oi_attrs_t attrs = ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
    int result = _anjay_dm_call_object_read_default_attrs(
            anjay, obj, _anjay_dm_current_ssid(anjay), &attrs, NULL);
    if (!result) {
        update_oi_attrs(&attrs, attributes);
        if (!oi_attrs_valid(&attrs)) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            result = _anjay_dm_call_object_write_default_attrs(
                    anjay, obj, _anjay_dm_current_ssid(anjay), &attrs, NULL);
        }
    }
    return result;
}

int _anjay_dm_write_attributes(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               const anjay_request_t *request) {
    dm_log(LAZY_DEBUG, _("Write Attributes ") "%s",
           ANJAY_DEBUG_MAKE_PATH(&request->uri));
    assert(_anjay_uri_path_has(&request->uri, ANJAY_ID_OID));
    if (request_attrs_empty(&request->attributes)) {
        return 0;
    }
    if (!_anjay_uri_path_has(&request->uri, ANJAY_ID_RID)
            && !resource_specific_request_attrs_empty(&request->attributes)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    int result;
    if (_anjay_uri_path_has(&request->uri, ANJAY_ID_IID)) {
        if (!(result = _anjay_dm_verify_instance_present(
                      anjay, obj, request->uri.ids[ANJAY_ID_IID]))) {
            if (!_anjay_instance_action_allowed(
                        anjay, &REQUEST_TO_ACTION_INFO(anjay, request))) {
                result = ANJAY_ERR_UNAUTHORIZED;
            } else if (_anjay_uri_path_has(&request->uri, ANJAY_ID_RIID)) {
                dm_log(ERROR,
                       _("Resource Instance Attributes not supported in this "
                         "version of Anjay"));
                return ANJAY_ERR_BAD_REQUEST;
            } else if (_anjay_uri_path_has(&request->uri, ANJAY_ID_RID)) {
                result = dm_write_resource_attrs(anjay, obj,
                                                 request->uri.ids[ANJAY_ID_IID],
                                                 request->uri.ids[ANJAY_ID_RID],
                                                 &request->attributes);
            } else {
                result = dm_write_instance_attrs(anjay, obj,
                                                 request->uri.ids[ANJAY_ID_IID],
                                                 &request->attributes);
            }
        }
    } else {
        result = dm_write_object_attrs(anjay, obj, &request->attributes);
    }
#ifdef ANJAY_WITH_OBSERVE
    if (!result) {
        // verify that new attributes are "seen" by the observe code
        result = _anjay_observe_notify(anjay, &request->uri,
                                       _anjay_dm_current_ssid(anjay), false);
    }
#endif // ANJAY_WITH_OBSERVE
    return result;
}
