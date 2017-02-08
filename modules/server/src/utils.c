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

#include "utils.h"

#include <string.h>

VISIBILITY_SOURCE_BEGIN

int _anjay_serv_fetch_ssid(anjay_input_ctx_t *ctx, anjay_ssid_t *out_ssid) {
    int32_t ssid;
    int retval = anjay_get_i32(ctx, &ssid);
    if (retval) {
        return retval;
    }
    *out_ssid = (anjay_ssid_t) ssid;
    return 0;
}

int _anjay_serv_fetch_validated_i32(anjay_input_ctx_t *ctx,
                                    int32_t min_value,
                                    int32_t max_value,
                                    int32_t *out_value) {
    int retval = anjay_get_i32(ctx, out_value);
    if (!retval && (*out_value < min_value || *out_value > max_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return retval;
}

int _anjay_serv_fetch_binding(anjay_input_ctx_t *ctx,
                              anjay_binding_mode_t *out_binding) {
    char buf[16];
    int retval;
    if ((retval = anjay_get_string(ctx, buf, sizeof(buf)))) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *out_binding = anjay_binding_mode_from_str(buf);
    return 0;
}

AVS_LIST(server_instance_t)
_anjay_serv_clone_instances(const server_repr_t *repr) {
    return AVS_LIST_SIMPLE_CLONE(repr->instances);
}

void _anjay_serv_destroy_instances(AVS_LIST(server_instance_t) *instances) {
    AVS_LIST_CLEAR(instances);
}

