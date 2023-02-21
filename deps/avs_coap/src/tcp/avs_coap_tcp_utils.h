/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_TCP_UTILS_H
#define AVS_COAP_SRC_TCP_UTILS_H

#include <avs_coap_init.h>

#include <stdint.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Converts @p payload, which may contain non-printable characters, to printable
 * string.
 *
 * @returns Number of bytes escaped. If it's not equal to @p payload_size, this
 *          function may be called again with @p payload pointer incremented by
 *          number of bytes escaped to convert further chunks of data.
 *
 * Note: @p converted message is always ended with NULL character.
 */
size_t _avs_coap_tcp_escape_payload(const char *payload,
                                    size_t payload_size,
                                    char *escaped_buf,
                                    size_t escaped_buf_size);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_TCP_UTILS_H
