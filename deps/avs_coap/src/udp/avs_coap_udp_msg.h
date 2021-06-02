/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

/**
 * Non-owning wrapper around a CoAP/UDP packet buffer.
 *
 * This is a representation of a parsed UDP CoAP message. Limited-size header
 * fields (the first 4 bytes, the token) are copied into respective fields,
 * while the dynamic-size fields (options and payload) are normally stored as
 * pointers into the original buffer.
 *
 * Objects of this type thus do NOT normally require explicit creation or
 * destruction.
 */
typedef struct {
    /**
     * The first four bytes of the UDP CoAP packet.
     *
     * When parsing an incoming packet, this information is copied.
     */
    avs_coap_udp_header_t header;

    /**
     * Token used to correlate requests and responses, if any.
     *
     * When parsing an incoming packet, this information is copied.
     */
    avs_coap_token_t token;

    /**
     * Structure describing the CoAP options present in the message.
     *
     * When parsing an incoming packet, this structure will describe a block of
     * data pointing inside the buffer being parsed. No actual data is copied.
     *
     * These options can be examined using the @ref avs_coap_options_get group
     * of functions declared in option.h.
     */
    avs_coap_options_t options;

    /**
     * Pointer to the start of content payload.
     *
     * It may be a non-NULL value even if the message has no payload.
     * Please use <c>payload_size</c> instead to check if payload is present.
     *
     * When parsing an incoming packet, this pointer will point inside the
     * buffer being parsed. No actual data is copied.
     */
    const void *payload;

    /**
     * Size of the content payload, i.e. number of valid bytes in the buffer
     * pointed to by the <c>payload</c> field.
     */
    size_t payload_size;
} avs_coap_udp_msg_t;

/**
 * Parses a UDP CoAP packet stored in a buffer.
 *
 * See the documentation of @ref avs_coap_udp_msg_t for more information about
 * the resulting structure.
 *
 * @param[out] out_msg     Pointer to a user-allocated structure that will be
 *                         filled with parsed information about the message.
 *
 * @param[in]  packet      Pointer to the packet data to parse.
 *
 * @param[in]  packet_size Size of the message pointed to by <c>packet</c>.
 *
 * @returns
 * - <c>AVS_OK</c> for success
 * - <c>{ AVS_COAP_ERR_CATEGORY, AVS_COAP_ERR_MALFORMED_OPTIONS }</c> if the
 *   CoAP options could not be parsed or are invalid
 * - <c>{ AVS_COAP_ERR_CATEGORY, AVS_COAP_ERR_MALFORMED_MESSAGE }</c> in case
 *   of other parsing failure
 */
avs_error_t _avs_coap_udp_msg_parse(avs_coap_udp_msg_t *out_msg,
                                    const void *packet,
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
