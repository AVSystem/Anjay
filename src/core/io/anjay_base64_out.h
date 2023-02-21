/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_BASE64_OUT_H
#define ANJAY_IO_BASE64_OUT_H

#include <avsystem/commons/avs_stream.h>

#include <anjay/io.h>

#include <anjay_modules/anjay_dm_utils.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

anjay_unlocked_ret_bytes_ctx_t *_anjay_base64_ret_bytes_ctx_new(
        avs_stream_t *stream, avs_base64_config_t config, size_t length);
int _anjay_base64_ret_bytes_ctx_close(anjay_unlocked_ret_bytes_ctx_t *ctx);

void _anjay_base64_ret_bytes_ctx_delete(anjay_unlocked_ret_bytes_ctx_t **ctx);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_BASE64_OUT_H */
