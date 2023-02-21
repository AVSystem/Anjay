/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SERVER_UTILS_H
#define SERVER_UTILS_H
#include <anjay_init.h>

#include <assert.h>

#include "anjay_mod_server.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

server_repr_t *_anjay_serv_get(const anjay_dm_installed_object_t obj_ptr);

int _anjay_serv_fetch_ssid(anjay_unlocked_input_ctx_t *ctx,
                           anjay_ssid_t *out_ssid);
int _anjay_serv_fetch_validated_i32(anjay_unlocked_input_ctx_t *ctx,
                                    int32_t min_value,
                                    int32_t max_value,
                                    int32_t *out_value);
int _anjay_serv_fetch_binding(anjay_unlocked_input_ctx_t *ctx,
                              anjay_binding_mode_t *out_binding);

AVS_LIST(server_instance_t)
_anjay_serv_clone_instances(const server_repr_t *repr);
void _anjay_serv_destroy_instances(AVS_LIST(server_instance_t) *instances);
void _anjay_serv_reset_instance(server_instance_t *serv);

VISIBILITY_PRIVATE_HEADER_END

#endif /* SERVER_UTILS_H */
