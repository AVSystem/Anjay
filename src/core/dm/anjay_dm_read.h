/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_READ_CORE_H
#define ANJAY_READ_CORE_H

#include <anjay/core.h>

#include "../anjay_dm_core.h"
#include "../anjay_io_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

anjay_msg_details_t
_anjay_dm_response_details_for_read(anjay_unlocked_t *anjay,
                                    const anjay_request_t *request,
                                    bool requires_hierarchical_format,
                                    anjay_lwm2m_version_t lwm2m_version);

int _anjay_dm_read_or_observe(anjay_connection_ref_t connection,
                              const anjay_dm_installed_object_t *obj,
                              const anjay_request_t *request);

int _anjay_dm_read(anjay_unlocked_t *anjay,
                   const anjay_dm_installed_object_t *obj,
                   const anjay_dm_path_info_t *path_info,
                   anjay_ssid_t requesting_ssid,
                   anjay_unlocked_output_ctx_t *out_ctx);

int _anjay_dm_read_and_destroy_ctx(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t *obj,
                                   const anjay_dm_path_info_t *path_info,
                                   anjay_ssid_t requesting_ssid,
                                   anjay_unlocked_output_ctx_t **out_ctx_ptr);

#ifdef ANJAY_WITH_LWM2M11
int _anjay_dm_read_or_observe_composite(anjay_connection_ref_t connection,
                                        const anjay_request_t *request,
                                        anjay_unlocked_input_ctx_t *in_ctx);
#endif // ANJAY_WITH_LWM2M11

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_READ_CORE_H
