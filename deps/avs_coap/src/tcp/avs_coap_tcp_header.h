/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_TCP_HEADER_H
#define AVS_COAP_SRC_TCP_HEADER_H

#include "avs_coap_common_utils.h"
#include "avs_coap_parse_utils.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

//  0           1           2           3           4           5           6
// +-----------+-----------+-----------+-----------+-----------+-----------+
// | Len | TKL | Extended Length (optional)                    | Code      |
// +-----------+-----------+-----------+-----------+-----------+-----------+
#define _AVS_COAP_TCP_MAX_HEADER_LENGTH 6
#define _AVS_COAP_TCP_MIN_HEADER_LENGTH 2

/** CoAP TCP message header. For internal use only. */
typedef struct avs_coap_tcp_header {
    uint64_t opts_and_payload_len;
    uint8_t token_len;
    uint8_t code;
} avs_coap_tcp_header_t;

size_t _avs_coap_tcp_header_serialize(const avs_coap_tcp_header_t *header,
                                      uint8_t *buf,
                                      size_t buf_size);

avs_error_t _avs_coap_tcp_header_parse(avs_coap_tcp_header_t *header,
                                       bytes_dispenser_t *dispenser,
                                       size_t *out_header_bytes_missing);

avs_coap_tcp_header_t _avs_coap_tcp_header_init(size_t payload_size,
                                                size_t options_size,
                                                uint8_t token_size,
                                                uint8_t code);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_TCP_HEADER_H
