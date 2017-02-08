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

#ifndef SECURITY_UTILS_H
#define SECURITY_UTILS_H
#include <config.h>

#include <assert.h>

#include <anjay/anjay.h>

#include <anjay_modules/utils.h>

#include "security.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

sec_repr_t *_anjay_sec_get(const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Fetches bytes from @p ctx. On success it frees underlying @p buffer storage
 * via @p _anjay_sec_raw_buffer_clear and reinitializes @p buffer properly with
 * obtained data.
 */
int _anjay_sec_fetch_bytes(anjay_input_ctx_t *ctx, anjay_raw_buffer_t *buffer);

/**
 * Fetches string from @p ctx. On success it calls free() on @p *out, and
 * reinitializes @p *out properly with a pointer to (heap allocated) obtained
 * data.
 */
int _anjay_sec_fetch_string(anjay_input_ctx_t *ctx, char **out);

/**
 * Fetches UDP Security Mode from @p ctx, performs validation and in case of
 * success sets @p *out to one of @p anjay_udp_security_mode_t enum value.
 */
int _anjay_sec_fetch_security_mode(anjay_input_ctx_t *ctx,
                                   anjay_udp_security_mode_t *out);

int _anjay_sec_validate_security_mode(int32_t security_mode);

/**
 * Fetches SSID from @p ctx, performs validation and in case of success sets
 * @p *out .
 */
int _anjay_sec_fetch_short_server_id(anjay_input_ctx_t *ctx, anjay_ssid_t *out);

/**
 * Frees all resources held in the @p instance.
 */
void _anjay_sec_destroy_instance_fields(sec_instance_t *instance);

/**
 * Frees all resources held in instances from the @p instances_ptr list,
 * and the list itself.
 */
void _anjay_sec_destroy_instances(AVS_LIST(sec_instance_t) *instances_ptr);

/**
 * Clones all instances of the given Security Object @p repr . Return NULL
 * if either there was nothing to clone or an error has occured.
 */
AVS_LIST(sec_instance_t) _anjay_sec_clone_instances(const sec_repr_t *repr);

VISIBILITY_PRIVATE_HEADER_END

#endif /* SECURITY_UTILS_H */
