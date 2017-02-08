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

#include <anjay_modules/utils.h>

#include "access_control.h"
#include "io.h"

VISIBILITY_SOURCE_BEGIN

static inline const anjay_dm_object_def_t *const *
get_access_control(anjay_t *anjay) {
    return _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_ACCESS_CONTROL);
}

static int read_u32(anjay_t *anjay,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    uint32_t *out) {
    int64_t ret;
    int result = _anjay_dm_res_read_i64(anjay,
            &(const anjay_resource_path_t) {
                .oid = ANJAY_DM_OID_ACCESS_CONTROL,
                .iid = iid,
                .rid = rid
            }, &ret);
    if (result) {
        return result;
    } else if (ret != (uint32_t) ret) {
        anjay_log(ERROR, "value overflow");
        return -1;
    }
    *out = (uint32_t) ret;
    return 0;
}

typedef struct {
    anjay_oid_t oid;
    anjay_iid_t oiid;
    anjay_ssid_t ssid;
    bool is_bootstrap;
    anjay_access_mask_t result;
} get_mask_data_t;

static int read_resources(anjay_t *anjay,
                          anjay_iid_t access_control_iid,
                          anjay_oid_t *out_oid,
                          anjay_iid_t *out_oiid,
                          anjay_ssid_t *out_owner) {
    uint32_t owner, oid, oiid;
    int ret;
    if ((ret = read_u32(anjay, access_control_iid,
            ANJAY_DM_RID_ACCESS_CONTROL_OID, &oid))
        || (ret = read_u32(anjay, access_control_iid,
            ANJAY_DM_RID_ACCESS_CONTROL_OIID, &oiid))
        || (ret = read_u32(anjay, access_control_iid,
            ANJAY_DM_RID_ACCESS_CONTROL_OWNER, &owner))) {
        return ret;
    }
    *out_oid = (anjay_oid_t) oid;
    *out_oiid = (anjay_iid_t) oiid;
    *out_owner = (anjay_ssid_t) owner;
    return 0;
}

static int get_mask_from_ctx(anjay_input_ctx_t *ctx,
                             anjay_ssid_t *inout_ssid,
                             anjay_access_mask_t *out_mask) {
    anjay_input_ctx_t *array_ctx = anjay_get_array(ctx);
    if (!array_ctx) {
        return -1;
    }

    anjay_ssid_t ssid_lookup = *inout_ssid;
    int32_t num_iterations = 0;
    int result = 0;
    uint16_t current_ssid;
    int32_t current_mask;
    *out_mask = ANJAY_ACCESS_MASK_NONE;
    while (!(result = anjay_get_array_index(array_ctx, &current_ssid))
            && !(result = anjay_get_i32(array_ctx, &current_mask))) {
        if (current_ssid == ssid_lookup || current_ssid == 0) {
            // Found an entry for the given ssid or the default ACL entry
            *inout_ssid = current_ssid;
            *out_mask = (anjay_access_mask_t) current_mask;
            if (current_ssid) {
                // not the default
                return 0;
            }
        }
        ++num_iterations;
    }
    // use the invalid SSID as a result if the ACL is empty
    *inout_ssid = (num_iterations ? 0 : UINT16_MAX);
    return result == ANJAY_GET_INDEX_END ? 0 : result;
}

static int get_mask(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj,
                    anjay_iid_t ac_iid,
                    void *in_data) {
    (void) obj;
    get_mask_data_t *data = (get_mask_data_t *) in_data;
    anjay_oid_t res_oid;
    anjay_iid_t res_oiid;
    anjay_ssid_t res_owner;

    if (read_resources(anjay, ac_iid, &res_oid, &res_oiid, &res_owner)) {
        return -1;
    }

    if ((res_oiid != data->oiid || res_oid != data->oid)
            || (data->is_bootstrap !=
                    (res_owner == ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP))) {
        return ANJAY_DM_FOREACH_CONTINUE;
    }

    const anjay_resource_path_t path = (anjay_resource_path_t){
        .oid = ANJAY_DM_OID_ACCESS_CONTROL,
        .iid = (anjay_iid_t) ac_iid,
        .rid = ANJAY_DM_RID_ACCESS_CONTROL_ACL
    };

    anjay_input_ctx_t *ctx = _anjay_dm_read_as_input_ctx(anjay, &path);
    if (!ctx) {
        return -1;
    }

    anjay_ssid_t found_ssid = data->ssid;
    anjay_access_mask_t mask;
    int result = get_mask_from_ctx(ctx, &found_ssid, &mask);
    _anjay_input_ctx_destroy(&ctx);

    if (result) {
        anjay_log(ERROR, "failed to read ACL!");
        return result;
    }

    if (found_ssid == data->ssid) {
        // Found the ACL
        data->result = mask;
        return ANJAY_DM_FOREACH_BREAK;
    } else if (found_ssid == UINT16_MAX) {
        if (res_owner == data->ssid) {
            // Empty ACL, and given ssid is an owner of the instance
            data->result = ANJAY_ACCESS_MASK_FULL & ~ANJAY_ACCESS_MASK_CREATE;
            return ANJAY_DM_FOREACH_BREAK;
        }
    } else if (!found_ssid) {
        // Default ACL
        data->result = mask;
    }
    // Not the iid we were looking for
    return ANJAY_DM_FOREACH_CONTINUE;
}

static anjay_access_mask_t
access_control_mask(anjay_t *anjay,
                    const anjay_action_info_t *info) {
    get_mask_data_t data = {
        .oid = info->oid,
        .oiid = info->iid,
        .ssid = info->ssid,
        .is_bootstrap = false,
        .result = ANJAY_ACCESS_MASK_NONE
    };

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_ACCESS_CONTROL);
    if (_anjay_dm_foreach_instance(anjay, obj, get_mask, &data)) {
        return ANJAY_ACCESS_MASK_NONE;
    }
    return data.result;
}

static bool can_instantiate(anjay_t *anjay,
                            const anjay_action_info_t *info) {
    get_mask_data_t data = {
        .oid = info->oid,
        .oiid = ANJAY_IID_INVALID,
        .ssid = info->ssid,
        .is_bootstrap = true,
        .result = ANJAY_ACCESS_MASK_NONE
    };

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_ACCESS_CONTROL);
    if (_anjay_dm_foreach_instance(anjay, obj, get_mask, &data)) {
        return false;
    }
    return data.result & ANJAY_ACCESS_MASK_CREATE;
}

typedef struct {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_ssid_t owner;
} get_owner_data_t;

static bool is_single_ssid_environment(anjay_t *anjay) {
    return _anjay_num_non_bootstrap_servers(anjay) == 1;
}

bool _anjay_access_control_action_allowed(anjay_t *anjay,
                                          const anjay_action_info_t *info) {
    if (info->oid == ANJAY_DM_OID_SECURITY) {
        return false;
    } else if (!get_access_control(anjay) || is_single_ssid_environment(anjay)) {
        return true;
    }

    if (info->oid == ANJAY_DM_OID_ACCESS_CONTROL) {
        if (info->action == ANJAY_ACTION_READ) {
            return true;
        } else if (info->action == ANJAY_ACTION_CREATE
                || info->action == ANJAY_ACTION_DELETE) {
            return false;
        }
        uint32_t owner;
        if (read_u32(anjay, info->iid, ANJAY_DM_RID_ACCESS_CONTROL_OWNER,
                     &owner)) {
            return false;
        }
        return (anjay_ssid_t) owner == info->ssid;
    }

    if (info->action == ANJAY_ACTION_CREATE) {
        return can_instantiate(anjay, info);
    }

    anjay_access_mask_t mask = access_control_mask(anjay, info);
    switch (info->action) {
    case ANJAY_ACTION_READ:
    case ANJAY_ACTION_DISCOVER:
        return mask & ANJAY_ACCESS_MASK_READ;
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
        return mask & ANJAY_ACCESS_MASK_WRITE;
    case ANJAY_ACTION_EXECUTE:
        return mask & ANJAY_ACCESS_MASK_EXECUTE;
    case ANJAY_ACTION_DELETE:
        return mask & ANJAY_ACCESS_MASK_DELETE;
    case ANJAY_ACTION_WRITE_ATTRIBUTES:
    case ANJAY_ACTION_CANCEL_OBSERVE:
        return true;
    default:
        assert(0 && "invalid enum value");
        return false;
    }
}
