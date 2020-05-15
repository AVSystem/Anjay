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

#ifndef AVS_COAP_SRC_UDP_UDP_MSG_H
#define AVS_COAP_SRC_UDP_UDP_MSG_H

#include <stddef.h>

#include <avsystem/coap/ctx.h>
#include <avsystem/coap/option.h>
#include <avsystem/coap/token.h>

#include "options/avs_coap_option.h"
#include "udp/avs_coap_udp_header.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/** Non-owning wrapper around a CoAP/UDP packet buffer. */
typedef struct {
    avs_coap_udp_header_t header;
    avs_coap_token_t token;
    avs_coap_options_t options;
    const void *payload;
    size_t payload_size;
} avs_coap_udp_msg_t;

avs_error_t _avs_coap_udp_msg_parse(avs_coap_udp_msg_t *out_msg,
                                    const uint8_t *packet,
                                    size_t packet_size);

/* Parses just the CoAP/UDP header and token */
void _avs_coap_udp_msg_parse_truncated(avs_coap_udp_msg_t *out_msg,
                                       const uint8_t *packet,
                                       size_t packet_size,
                                       bool *out_has_token,
                                       bool *out_has_options);

avs_error_t _avs_coap_udp_msg_serialize(const avs_coap_udp_msg_t *msg,
                                        uint8_t *buf,
                                        size_t buf_size,
                                        size_t *out_bytes_written);

avs_error_t _avs_coap_udp_msg_copy(const avs_coap_udp_msg_t *src,
                                   avs_coap_udp_msg_t *dst,
                                   uint8_t *packet_buf,
                                   size_t packet_buf_size);

static inline size_t _avs_coap_udp_msg_size(const avs_coap_udp_msg_t *msg) {
    return sizeof(msg->header) + msg->token.size + msg->options.size
           + (msg->payload_size == 0
                      ? 0
                      : sizeof(AVS_COAP_PAYLOAD_MARKER) + msg->payload_size);
}

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_UDP_UDP_MSG_H
