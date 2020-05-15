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

#ifndef SERVER_UTILS_H
#define SERVER_UTILS_H
#include <anjay_init.h>

#include <assert.h>

#include <anjay/core.h>

#include <anjay_modules/anjay_raw_buffer.h>

#include "anjay_mod_server.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

server_repr_t *_anjay_serv_get(const anjay_dm_object_def_t *const *obj_ptr);

int _anjay_serv_fetch_ssid(anjay_input_ctx_t *ctx, anjay_ssid_t *out_ssid);
int _anjay_serv_fetch_validated_i32(anjay_input_ctx_t *ctx,
                                    int32_t min_value,
                                    int32_t max_value,
                                    int32_t *out_value);
int _anjay_serv_fetch_binding(anjay_input_ctx_t *ctx,
                              anjay_binding_mode_t *out_binding);

AVS_LIST(server_instance_t)
_anjay_serv_clone_instances(const server_repr_t *repr);
void _anjay_serv_destroy_instances(AVS_LIST(server_instance_t) *instances);
void _anjay_serv_reset_instance(server_instance_t *serv);

VISIBILITY_PRIVATE_HEADER_END

#endif /* SERVER_UTILS_H */
