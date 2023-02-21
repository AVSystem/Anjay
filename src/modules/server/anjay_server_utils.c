/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_SERVER

#    include <anjay_modules/anjay_dm_utils.h>

#    include "anjay_server_utils.h"

#    include <string.h>

VISIBILITY_SOURCE_BEGIN

int _anjay_serv_fetch_ssid(anjay_unlocked_input_ctx_t *ctx,
                           anjay_ssid_t *out_ssid) {
    int32_t ssid;
    int retval = _anjay_get_i32_unlocked(ctx, &ssid);
    if (retval) {
        return retval;
    }
    *out_ssid = (anjay_ssid_t) ssid;
    return 0;
}

int _anjay_serv_fetch_validated_i32(anjay_unlocked_input_ctx_t *ctx,
                                    int32_t min_value,
                                    int32_t max_value,
                                    int32_t *out_value) {
    int retval = _anjay_get_i32_unlocked(ctx, out_value);
    if (!retval && (*out_value < min_value || *out_value > max_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return retval;
}

int _anjay_serv_fetch_binding(anjay_unlocked_input_ctx_t *ctx,
                              anjay_binding_mode_t *out_binding) {
    int retval;
    if ((retval = _anjay_get_string_unlocked(
                 ctx, out_binding->data, sizeof(out_binding->data)))) {
        return retval;
    }
    return anjay_binding_mode_valid(out_binding->data) ? 0
                                                       : ANJAY_ERR_BAD_REQUEST;
}

AVS_LIST(server_instance_t)
_anjay_serv_clone_instances(const server_repr_t *repr) {
    return AVS_LIST_SIMPLE_CLONE(repr->instances);
}

void _anjay_serv_destroy_instances(AVS_LIST(server_instance_t) *instances) {
    AVS_LIST_CLEAR(instances);
}

void _anjay_serv_reset_instance(server_instance_t *serv) {
    const anjay_iid_t iid = serv->iid;
    memset(serv, 0, sizeof(*serv));
    /* This is not a resource, therefore must be restored */
    serv->iid = iid;
    serv->present_resources[SERV_RES_REGISTRATION_UPDATE_TRIGGER] = true;
#    ifndef ANJAY_WITHOUT_DEREGISTER
    serv->present_resources[SERV_RES_DISABLE] = true;
#    endif // ANJAY_WITHOUT_DEREGISTER
#    ifdef ANJAY_WITH_LWM2M11
    serv->bootstrap_on_registration_failure = true;
    serv->present_resources[SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE] = true;
#        ifdef ANJAY_WITH_BOOTSTRAP
    serv->present_resources[SERV_RES_BOOTSTRAP_REQUEST_TRIGGER] = true;
#        endif // ANJAY_WITH_BOOTSTRAP
#        ifdef ANJAY_WITH_SEND
    serv->present_resources[SERV_RES_MUTE_SEND] = true;
#        endif // ANJAY_WITH_SEND
#    endif     // ANJAY_WITH_LWM2M11
}

#endif // ANJAY_WITH_MODULE_SERVER
