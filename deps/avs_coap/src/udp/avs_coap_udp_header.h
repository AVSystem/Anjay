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

#ifndef AVS_COAP_SRC_UDP_UDP_HEADER_H
#define AVS_COAP_SRC_UDP_UDP_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include <avsystem/coap/token.h>

#include "avs_coap_parse_utils.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/** CoAP message type, as defined in RFC7252. */
typedef enum avs_coap_udp_type {
    AVS_COAP_UDP_TYPE_CONFIRMABLE,
    AVS_COAP_UDP_TYPE_NON_CONFIRMABLE,
    AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
    AVS_COAP_UDP_TYPE_RESET,

    _AVS_COAP_UDP_TYPE_FIRST = AVS_COAP_UDP_TYPE_CONFIRMABLE,
    _AVS_COAP_UDP_TYPE_LAST = AVS_COAP_UDP_TYPE_RESET
} avs_coap_udp_type_t;

/** Serialized CoAP message header. For internal use only. */
typedef struct avs_coap_udp_header {
    uint8_t version_type_token_length;
    uint8_t code;
    uint8_t message_id[2];
} avs_coap_udp_header_t;

AVS_STATIC_ASSERT(AVS_ALIGNOF(avs_coap_udp_header_t) == 1,
                  avs_coap_udp_header_t_must_always_be_properly_aligned);

/** @{
 * Sanity checks that ensure no padding is inserted anywhere inside
 * @ref avs_coap_udp_header_t .
 */
AVS_STATIC_ASSERT(offsetof(avs_coap_udp_header_t, version_type_token_length)
                          == 0,
                  vttl_field_is_at_start_of_avs_coap_udp_header_t);
AVS_STATIC_ASSERT(offsetof(avs_coap_udp_header_t, code) == 1,
                  no_padding_before_code_field_of_avs_coap_udp_header_t);
AVS_STATIC_ASSERT(offsetof(avs_coap_udp_header_t, message_id) == 2,
                  no_padding_before_message_id_field_of_avs_coap_udp_header_t);
AVS_STATIC_ASSERT(sizeof(avs_coap_udp_header_t) == 4,
                  no_padding_in_avs_coap_udp_header_t);
/** @} */

#define _AVS_COAP_UDP_HEADER_VERSION_MASK 0xC0
#define _AVS_COAP_UDP_HEADER_VERSION_SHIFT 6

static inline uint8_t
_avs_coap_udp_header_get_version(const avs_coap_udp_header_t *hdr) {
    int val = _AVS_FIELD_GET(hdr->version_type_token_length,
                             _AVS_COAP_UDP_HEADER_VERSION_MASK,
                             _AVS_COAP_UDP_HEADER_VERSION_SHIFT);
    assert(val >= 0 && val <= 3);
    return (uint8_t) val;
}

static inline void _avs_coap_udp_header_set_version(avs_coap_udp_header_t *hdr,
                                                    uint8_t version) {
    assert(version <= 3);
    _AVS_FIELD_SET(hdr->version_type_token_length,
                   _AVS_COAP_UDP_HEADER_VERSION_MASK,
                   _AVS_COAP_UDP_HEADER_VERSION_SHIFT, version);
}

#define _AVS_COAP_UDP_HEADER_TOKEN_LENGTH_MASK 0x0F
#define _AVS_COAP_UDP_HEADER_TOKEN_LENGTH_SHIFT 0

static inline uint8_t
_avs_coap_udp_header_get_token_length(const avs_coap_udp_header_t *hdr) {
    int val = _AVS_FIELD_GET(hdr->version_type_token_length,
                             _AVS_COAP_UDP_HEADER_TOKEN_LENGTH_MASK,
                             _AVS_COAP_UDP_HEADER_TOKEN_LENGTH_SHIFT);
    assert(val >= 0 && val <= _AVS_COAP_UDP_HEADER_TOKEN_LENGTH_MASK);
    return (uint8_t) val;
}

static inline void
_avs_coap_udp_header_set_token_length(avs_coap_udp_header_t *hdr,
                                      uint8_t token_length) {
    assert(token_length <= AVS_COAP_MAX_TOKEN_LENGTH);
    _AVS_FIELD_SET(hdr->version_type_token_length,
                   _AVS_COAP_UDP_HEADER_TOKEN_LENGTH_MASK,
                   _AVS_COAP_UDP_HEADER_TOKEN_LENGTH_SHIFT, token_length);
}

/** @{
 * Internal macros used for retrieving CoAP message type from
 * @ref coap_msg_header_t .
 */
#define _AVS_COAP_UDP_HEADER_TYPE_MASK 0x30
#define _AVS_COAP_UDP_HEADER_TYPE_SHIFT 4
/** @} */

static inline avs_coap_udp_type_t
_avs_coap_udp_header_get_type(const avs_coap_udp_header_t *hdr) {
    int val = _AVS_FIELD_GET(hdr->version_type_token_length,
                             _AVS_COAP_UDP_HEADER_TYPE_MASK,
                             _AVS_COAP_UDP_HEADER_TYPE_SHIFT);
    assert(val >= _AVS_COAP_UDP_TYPE_FIRST && val <= _AVS_COAP_UDP_TYPE_LAST);
    return (avs_coap_udp_type_t) val;
}

static inline void _avs_coap_udp_header_set_type(avs_coap_udp_header_t *hdr,
                                                 avs_coap_udp_type_t type) {
    _AVS_FIELD_SET(hdr->version_type_token_length,
                   _AVS_COAP_UDP_HEADER_TYPE_MASK,
                   _AVS_COAP_UDP_HEADER_TYPE_SHIFT, type);
}

static inline uint16_t
_avs_coap_udp_header_get_id(const avs_coap_udp_header_t *hdr) {
    return extract_u16(hdr->message_id);
}

static inline void _avs_coap_udp_header_set_id(avs_coap_udp_header_t *hdr,
                                               uint16_t msg_id) {
    uint16_t msg_id_nbo = avs_convert_be16(msg_id);
    memcpy(hdr->message_id, &msg_id_nbo, sizeof(msg_id_nbo));
}

static inline void _avs_coap_udp_header_set(avs_coap_udp_header_t *hdr,
                                            avs_coap_udp_type_t type,
                                            uint8_t token_length,
                                            uint8_t code,
                                            uint16_t id) {
    _avs_coap_udp_header_set_version(hdr, 1);
    _avs_coap_udp_header_set_type(hdr, type);
    _avs_coap_udp_header_set_token_length(hdr, token_length);
    hdr->code = code;
    _avs_coap_udp_header_set_id(hdr, id);
}

static inline avs_coap_udp_header_t
_avs_coap_udp_header_init(avs_coap_udp_type_t type,
                          uint8_t token_length,
                          uint8_t code,
                          uint16_t id) {
    avs_coap_udp_header_t hdr = { 0 };
    _avs_coap_udp_header_set(&hdr, type, token_length, code, id);
    return hdr;
}

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_UDP_UDP_HEADER_H
