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

#ifndef ANJAY_READ_CORE_H
#define ANJAY_READ_CORE_H

#include <anjay/core.h>

#include "../anjay_dm_core.h"
#include "../anjay_io_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

anjay_msg_details_t
_anjay_dm_response_details_for_read(anjay_t *anjay,
                                    const anjay_request_t *request,
                                    bool requires_hierarchical_format,
                                    anjay_lwm2m_version_t lwm2m_version);

int _anjay_dm_read_or_observe(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              const anjay_request_t *request);

int _anjay_dm_read(anjay_t *anjay,
                   const anjay_dm_object_def_t *const *obj,
                   const anjay_dm_path_info_t *path_info,
                   anjay_ssid_t requesting_ssid,
                   anjay_output_ctx_t *out_ctx);

int _anjay_dm_read_and_destroy_ctx(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   const anjay_dm_path_info_t *path_info,
                                   anjay_ssid_t requesting_ssid,
                                   anjay_output_ctx_t **out_ctx_ptr);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_READ_CORE_H
