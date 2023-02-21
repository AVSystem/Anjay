/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_CREATE_CORE_H
#define ANJAY_CREATE_CORE_H

#include <anjay/core.h>

#include "../anjay_dm_core.h"
#include "../anjay_io_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_dm_create(anjay_unlocked_t *anjay,
                     const anjay_dm_installed_object_t *obj,
                     const anjay_request_t *request,
                     anjay_ssid_t ssid,
                     anjay_unlocked_input_ctx_t *in_ctx);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_CREATE_CORE_H
