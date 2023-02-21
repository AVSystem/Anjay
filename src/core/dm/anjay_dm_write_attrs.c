/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include "anjay_dm_write_attrs.h"

#include "../anjay_access_utils_private.h"

VISIBILITY_SOURCE_BEGIN

static void update_oi_attrs(anjay_dm_oi_attributes_t *attrs_ptr,
                            const anjay_request_attributes_t *request_attrs) {
    if (request_attrs->has_min_period) {
        attrs_ptr->min_period = request_attrs->values.common.min_period;
    }
    if (request_attrs->has_max_period) {
        attrs_ptr->max_period = request_attrs->values.common.max_period;
    }
    if (request_attrs->has_min_eval_period) {
        attrs_ptr->min_eval_period =
                request_attrs->values.common.min_eval_period;
    }
    if (request_attrs->has_max_eval_period) {
        attrs_ptr->max_eval_period =
                request_attrs->values.common.max_eval_period;
    }
#ifdef ANJAY_WITH_CON_ATTR
    if (request_attrs->has_con) {
        attrs_ptr->con = request_attrs->values.common.con;
    }
#endif
}

void _anjay_update_r_attrs(anjay_dm_r_attributes_t *attrs_ptr,
                           const anjay_request_attributes_t *request_attrs) {
    update_oi_attrs(&attrs_ptr->common, request_attrs);
    if (request_attrs->has_greater_than) {
        attrs_ptr->greater_than = request_attrs->values.greater_than;
    }
    if (request_attrs->has_less_than) {
        attrs_ptr->less_than = request_attrs->values.less_than;
    }
    if (request_attrs->has_step) {
        attrs_ptr->step = request_attrs->values.step;
    }
}

static bool oi_attrs_valid(const anjay_dm_oi_attributes_t *attrs) {
    if (attrs->min_eval_period >= 0 && attrs->max_eval_period >= 0
            && attrs->min_eval_period >= attrs->max_eval_period) {
        dm_log(DEBUG, _("Attempted to set attributes that fail the 'epmin < "
                        "epmax' precondition"));
        return false;
    }
    return true;
}

bool _anjay_r_attrs_valid(const anjay_dm_r_attributes_t *attrs) {
    if (!oi_attrs_valid(&attrs->common)) {
        return false;
    }

    double step = 0.0;
    if (!isnan(attrs->step)) {
        if (attrs->step < 0.0) {
            dm_log(DEBUG, _("Attempted to set negative step attribute"));
            return false;
        }
        step = attrs->step;
    }
    if (!isnan(attrs->less_than) && !isnan(attrs->greater_than)
            && attrs->less_than + 2 * step >= attrs->greater_than) {
        dm_log(DEBUG, _("Attempted to set attributes that fail the 'lt + 2*st "
                        "< gt' precondition"));
        return false;
    }
    return true;
}

bool _anjay_dm_resource_specific_request_attrs_empty(
        const anjay_request_attributes_t *attrs) {
    return !attrs->has_greater_than && !attrs->has_less_than
           && !attrs->has_step;
}

bool _anjay_dm_request_attrs_empty(const anjay_request_attributes_t *attrs) {
    return !attrs->has_min_period && !attrs->has_max_period
           && !attrs->has_min_eval_period && !attrs->has_max_eval_period
#ifdef ANJAY_WITH_CON_ATTR
           && !attrs->has_con
#endif
           && _anjay_dm_resource_specific_request_attrs_empty(attrs);
}

#ifdef ANJAY_WITH_LWM2M11
static int
dm_write_resource_instance_attrs(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t *obj,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 anjay_ssid_t ssid,
                                 const anjay_request_attributes_t *attributes) {
    anjay_dm_r_attributes_t attrs = ANJAY_DM_R_ATTRIBUTES_EMPTY;
    int result;
    (void) ((result = _anjay_dm_verify_resource_instance_present(
                     anjay, obj, iid, rid, riid))
            || (result = _anjay_dm_call_resource_instance_read_attrs(
                        anjay, obj, iid, rid, riid, ssid, &attrs)));
    if (!result) {
        _anjay_update_r_attrs(&attrs, attributes);
        if (!_anjay_r_attrs_valid(&attrs)) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            result = _anjay_dm_call_resource_instance_write_attrs(
                    anjay, obj, iid, rid, riid, ssid, &attrs);
        }
    }
    return result;
}
#endif // ANJAY_WITH_LWM2M11

static int
dm_write_resource_attrs(anjay_unlocked_t *anjay,
                        const anjay_dm_installed_object_t *obj,
                        anjay_iid_t iid,
                        anjay_rid_t rid,
                        anjay_ssid_t ssid,
                        const anjay_request_attributes_t *attributes) {
    anjay_dm_r_attributes_t attrs = ANJAY_DM_R_ATTRIBUTES_EMPTY;
    int result;
    (void) ((result = _anjay_dm_verify_resource_present(anjay, obj, iid, rid,
                                                        NULL))
            || (result = _anjay_dm_call_resource_read_attrs(
                        anjay, obj, iid, rid, ssid, &attrs)));
    if (!result) {
        _anjay_update_r_attrs(&attrs, attributes);
        if (!_anjay_r_attrs_valid(&attrs)) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            result = _anjay_dm_call_resource_write_attrs(anjay, obj, iid, rid,
                                                         ssid, &attrs);
        }
    }
    return result;
}

static int
dm_write_instance_attrs(anjay_unlocked_t *anjay,
                        const anjay_dm_installed_object_t *obj,
                        anjay_iid_t iid,
                        anjay_ssid_t ssid,
                        const anjay_request_attributes_t *attributes) {
    anjay_dm_oi_attributes_t attrs = ANJAY_DM_OI_ATTRIBUTES_EMPTY;
    int result = _anjay_dm_call_instance_read_default_attrs(anjay, obj, iid,
                                                            ssid, &attrs);
    if (!result) {
        update_oi_attrs(&attrs, attributes);
        if (!oi_attrs_valid(&attrs)) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            result =
                    _anjay_dm_call_instance_write_default_attrs(anjay, obj, iid,
                                                                ssid, &attrs);
        }
    }
    return result;
}

static int dm_write_object_attrs(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t *obj,
                                 anjay_ssid_t ssid,
                                 const anjay_request_attributes_t *attributes) {
    anjay_dm_oi_attributes_t attrs = ANJAY_DM_OI_ATTRIBUTES_EMPTY;
    int result =
            _anjay_dm_call_object_read_default_attrs(anjay, obj, ssid, &attrs);
    if (!result) {
        update_oi_attrs(&attrs, attributes);
        if (!oi_attrs_valid(&attrs)) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            result = _anjay_dm_call_object_write_default_attrs(anjay, obj, ssid,
                                                               &attrs);
        }
    }
    return result;
}

int _anjay_dm_write_attributes(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t *obj,
                               const anjay_request_t *request,
                               anjay_ssid_t ssid) {
    dm_log(LAZY_DEBUG, _("Write Attributes ") "%s",
           ANJAY_DEBUG_MAKE_PATH(&request->uri));
    assert(_anjay_uri_path_has(&request->uri, ANJAY_ID_OID));
    if (_anjay_dm_request_attrs_empty(&request->attributes)) {
        return 0;
    }
    if (!_anjay_uri_path_has(&request->uri, ANJAY_ID_RID)
            && !_anjay_dm_resource_specific_request_attrs_empty(
                       &request->attributes)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    int result;
    if (_anjay_uri_path_has(&request->uri, ANJAY_ID_IID)) {
        if (!(result = _anjay_dm_verify_instance_present(
                      anjay, obj, request->uri.ids[ANJAY_ID_IID]))) {
            if (!_anjay_instance_action_allowed(
                        anjay, &REQUEST_TO_ACTION_INFO(request, ssid))) {
                result = ANJAY_ERR_UNAUTHORIZED;
            } else if (_anjay_uri_path_has(&request->uri, ANJAY_ID_RIID)) {
#ifdef ANJAY_WITH_LWM2M11
                result = dm_write_resource_instance_attrs(
                        anjay, obj, request->uri.ids[ANJAY_ID_IID],
                        request->uri.ids[ANJAY_ID_RID],
                        request->uri.ids[ANJAY_ID_RIID], ssid,
                        &request->attributes);
#else  // ANJAY_WITH_LWM2M11
                dm_log(ERROR,
                       _("Resource Instance Attributes not supported in this "
                         "version of Anjay"));
                return ANJAY_ERR_BAD_REQUEST;
#endif // ANJAY_WITH_LWM2M11
            } else if (_anjay_uri_path_has(&request->uri, ANJAY_ID_RID)) {
                result = dm_write_resource_attrs(anjay, obj,
                                                 request->uri.ids[ANJAY_ID_IID],
                                                 request->uri.ids[ANJAY_ID_RID],
                                                 ssid, &request->attributes);
            } else {
                result = dm_write_instance_attrs(anjay, obj,
                                                 request->uri.ids[ANJAY_ID_IID],
                                                 ssid, &request->attributes);
            }
        }
    } else {
        result = dm_write_object_attrs(anjay, obj, ssid, &request->attributes);
    }
#ifdef ANJAY_WITH_OBSERVE
    if (!result) {
        // verify that new attributes are "seen" by the observe code
        result = _anjay_observe_notify(anjay, &request->uri, ssid, false);
    }
#endif // ANJAY_WITH_OBSERVE
    return result;
}
