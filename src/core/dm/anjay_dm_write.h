/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_WRITE_CORE_H
#define ANJAY_WRITE_CORE_H

#include <anjay/core.h>

#include "../anjay_dm_core.h"
#include "../anjay_io_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_dm_write(anjay_unlocked_t *anjay,
                    const anjay_dm_installed_object_t *obj,
                    const anjay_request_t *request,
                    anjay_ssid_t ssid,
                    anjay_unlocked_input_ctx_t *in_ctx);

#if defined(ANJAY_WITH_LWM2M11) && !defined(ANJAY_WITHOUT_COMPOSITE_OPERATIONS)
int _anjay_dm_write_composite(anjay_unlocked_t *anjay,
                              const anjay_request_t *request,
                              anjay_ssid_t ssid,
                              anjay_unlocked_input_ctx_t *in_ctx);
#endif // defined(ANJAY_WITH_LWM2M11) &&
       // !defined(ANJAY_WITHOUT_COMPOSITE_OPERATIONS)

/**
 * NOTE: This function is used in one situation, that is: after LwM2M Create to
 * initialize newly created Instance with data contained within the request
 * payload (handled by input context @p in_ctx).
 *
 * Apart from that, the function has no more applications and @ref
 * _anjay_dm_write() shall be used instead.
 */
int _anjay_dm_write_created_instance_and_move_to_next_entry(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        anjay_iid_t iid,
        anjay_unlocked_input_ctx_t *in_ctx);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_WRITE_CORE_H
