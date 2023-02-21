/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_DM_EXECUTE_H
#define ANJAY_INCLUDE_ANJAY_MODULES_DM_EXECUTE_H

#include <anjay/io.h>

#include <anjay_modules/dm/anjay_modules.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

anjay_unlocked_execute_ctx_t *
_anjay_execute_ctx_create(avs_stream_t *payload_stream);
void _anjay_execute_ctx_destroy(anjay_unlocked_execute_ctx_t **ctx);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_DM_EXECUTE_H */
