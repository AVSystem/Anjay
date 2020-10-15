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

#ifndef SECURITY_UTILS_H
#define SECURITY_UTILS_H
#include <anjay_init.h>

#include <assert.h>

#include <anjay/core.h>

#include <anjay_modules/anjay_raw_buffer.h>

#include "anjay_mod_security.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

sec_repr_t *_anjay_sec_get(const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Fetches UDP Security Mode from @p ctx, performs validation and in case of
 * success sets @p *out to one of @p anjay_security_mode_t enum value.
 */
int _anjay_sec_fetch_security_mode(anjay_input_ctx_t *ctx,
                                   anjay_security_mode_t *out);

int _anjay_sec_validate_security_mode(int32_t security_mode);

/**
 * Fetches SMS Security Mode from @p ctx, performs validation and in case of
 * success sets @p *out to one of @p anjay_sms_security_mode_t enum value.
 */
int _anjay_sec_fetch_sms_security_mode(anjay_input_ctx_t *ctx,
                                       anjay_sms_security_mode_t *out);

int _anjay_sec_validate_sms_security_mode(int32_t security_mode);

/**
 * Fetches SSID from @p ctx, performs validation and in case of success sets
 * @p *out .
 */
int _anjay_sec_fetch_short_server_id(anjay_input_ctx_t *ctx, anjay_ssid_t *out);

void _anjay_sec_key_or_data_cleanup(sec_key_or_data_t *value);

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
 * if either there was nothing to clone or an error has occurred.
 */
AVS_LIST(sec_instance_t) _anjay_sec_clone_instances(const sec_repr_t *repr);

VISIBILITY_PRIVATE_HEADER_END

#endif /* SECURITY_UTILS_H */
