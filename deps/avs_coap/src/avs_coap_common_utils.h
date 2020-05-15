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

#ifndef AVS_COAP_SRC_COMMON_UTILS_H
#define AVS_COAP_SRC_COMMON_UTILS_H

#include "avs_coap_ctx.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    uint8_t *write_ptr;
    size_t bytes_left;
} bytes_appender_t;

typedef struct {
    const uint8_t *read_ptr;
    size_t bytes_left;
} bytes_dispenser_t;

int _avs_coap_bytes_append(bytes_appender_t *appender,
                           const void *data,
                           size_t size_bytes);

int _avs_coap_bytes_extract(bytes_dispenser_t *dispenser,
                            void *out,
                            size_t size_bytes);

avs_error_t _avs_coap_parse_token(avs_coap_token_t *out_token,
                                  uint8_t token_size,
                                  bytes_dispenser_t *dispenser);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_COMMON_UTILS_H
