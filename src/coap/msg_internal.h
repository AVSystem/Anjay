/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_COAP_MSG_INTERNAL_H
#define ANJAY_COAP_MSG_INTERNAL_H

#include "msg.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ANJAY_COAP_HEADER_VERSION_MASK 0xC0
#define ANJAY_COAP_HEADER_VERSION_SHIFT 6
#define ANJAY_COAP_HEADER_TOKEN_LENGTH_MASK 0x0F
#define ANJAY_COAP_HEADER_TOKEN_LENGTH_SHIFT 0

static inline uint8_t
_anjay_coap_msg_header_get_version(const anjay_coap_msg_header_t *hdr) {
    int val = ANJAY_FIELD_GET(hdr->version_type_token_length,
                              ANJAY_COAP_HEADER_VERSION_MASK,
                              ANJAY_COAP_HEADER_VERSION_SHIFT);
    assert(val >= 0 && val <= 3);
    return (uint8_t)val;
}
static inline void
_anjay_coap_msg_header_set_version(anjay_coap_msg_header_t *hdr,
                                   uint8_t version) {
    assert(version <= 3);
    ANJAY_FIELD_SET(hdr->version_type_token_length,
                    ANJAY_COAP_HEADER_VERSION_MASK,
                    ANJAY_COAP_HEADER_VERSION_SHIFT, version);
}

static inline uint8_t
_anjay_coap_msg_header_get_token_length(const anjay_coap_msg_header_t *hdr) {
    int val = ANJAY_FIELD_GET(hdr->version_type_token_length,
                              ANJAY_COAP_HEADER_TOKEN_LENGTH_MASK,
                              ANJAY_COAP_HEADER_TOKEN_LENGTH_SHIFT);
    assert(val >= 0 && val <= ANJAY_COAP_HEADER_TOKEN_LENGTH_MASK);
    return (uint8_t)val;
}
static inline void
_anjay_coap_msg_header_set_token_length(anjay_coap_msg_header_t *hdr,
                                        uint8_t token_length) {
    assert(token_length <= ANJAY_COAP_MAX_TOKEN_LENGTH);
    ANJAY_FIELD_SET(hdr->version_type_token_length,
                    ANJAY_COAP_HEADER_TOKEN_LENGTH_MASK,
                    ANJAY_COAP_HEADER_TOKEN_LENGTH_SHIFT, token_length);
}

#define ANJAY_COAP_OPT_DELTA_MASK 0xF0
#define ANJAY_COAP_OPT_DELTA_SHIFT 4
#define ANJAY_COAP_OPT_LENGTH_MASK 0x0F
#define ANJAY_COAP_OPT_LENGTH_SHIFT 0

static inline uint8_t _anjay_coap_opt_get_short_delta(const anjay_coap_opt_t *opt) {
    return ANJAY_FIELD_GET(opt->delta_length,
                           ANJAY_COAP_OPT_DELTA_MASK,
                           ANJAY_COAP_OPT_DELTA_SHIFT);
}

static inline void _anjay_coap_opt_set_short_delta(anjay_coap_opt_t *opt,
                                                   uint8_t delta) {
    assert(delta <= ANJAY_COAP_EXT_RESERVED);
    ANJAY_FIELD_SET(opt->delta_length,
                    ANJAY_COAP_OPT_DELTA_MASK,
                    ANJAY_COAP_OPT_DELTA_SHIFT, delta);
}

static inline uint8_t _anjay_coap_opt_get_short_length(const anjay_coap_opt_t *opt) {
    return ANJAY_FIELD_GET(opt->delta_length,
                           ANJAY_COAP_OPT_LENGTH_MASK,
                           ANJAY_COAP_OPT_LENGTH_SHIFT);
}

static inline void _anjay_coap_opt_set_short_length(anjay_coap_opt_t *opt,
                                                    uint8_t length) {
    assert(length <= ANJAY_COAP_EXT_RESERVED);
    ANJAY_FIELD_SET(opt->delta_length,
                    ANJAY_COAP_OPT_LENGTH_MASK,
                    ANJAY_COAP_OPT_LENGTH_SHIFT, length);
}

static inline size_t _anjay_coap_get_opt_header_size(uint16_t opt_number_delta,
                                                     uint16_t opt_data_size) {
    size_t header_size = 1;

    if (opt_number_delta >= ANJAY_COAP_EXT_U16_BASE) {
        header_size += 2;
    } else if (opt_number_delta >= ANJAY_COAP_EXT_U8_BASE) {
        header_size += 1;
    }

    if (opt_data_size >= ANJAY_COAP_EXT_U16_BASE) {
        header_size += 2;
    } else if (opt_data_size >= ANJAY_COAP_EXT_U8_BASE) {
        header_size += 1;
    }

    return header_size;
}

struct anjay_coap_msg_info_opt {
    uint16_t number;
    uint16_t data_size;
    uint8_t data[];
};

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_MSG_INTERNAL_H
