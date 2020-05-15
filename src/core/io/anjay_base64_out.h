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

#ifndef ANJAY_IO_BASE64_OUT_H
#define ANJAY_IO_BASE64_OUT_H

#include <avsystem/commons/avs_stream.h>

#include <anjay/io.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

anjay_ret_bytes_ctx_t *_anjay_base64_ret_bytes_ctx_new(
        avs_stream_t *stream, avs_base64_config_t config, size_t length);
int _anjay_base64_ret_bytes_ctx_close(anjay_ret_bytes_ctx_t *ctx);

void _anjay_base64_ret_bytes_ctx_delete(anjay_ret_bytes_ctx_t **ctx);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_BASE64_OUT_H */
