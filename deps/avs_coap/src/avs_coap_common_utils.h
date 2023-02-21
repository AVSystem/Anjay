/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
